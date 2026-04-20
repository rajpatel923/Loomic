#include "LoomicServer/ws/WebSocketSession.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/tcp/frame.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <charconv>
#include <chrono>
#include <cstring>
#include <span>
#include <string>

namespace beast = boost::beast;
namespace net   = boost::asio;

namespace Loomic {

WebSocketSession::WebSocketSession(net::ip::tcp::socket     socket,
                                   std::shared_ptr<SessionRegistry> registry,
                                   std::shared_ptr<JwtService>      jwt,
                                   std::shared_ptr<RedisClient>     redis,
                                   std::shared_ptr<CassandraClient> cass,
                                   std::shared_ptr<SnowflakeGen>    snowflake,
                                   std::shared_ptr<PgPool>          pg,
                                   std::string                      server_id)
    : ws_(beast::tcp_stream(std::move(socket)))
    , strand_(net::make_strand(ws_.get_executor()))
    , heartbeat_(strand_)
    , registry_(std::move(registry))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
    , cass_(std::move(cass))
    , snowflake_(std::move(snowflake))
    , pg_(std::move(pg))
    , server_id_(std::move(server_id))
{}

// ── run ───────────────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::run(beast::flat_buffer buffer,
                                            Request            upgrade_req)
{
    auto self = shared_from_this();

    try {
        // 1. Complete the WebSocket handshake
        co_await ws_.async_accept(upgrade_req, net::use_awaitable);

        // 2. Read first text frame — must be {"type":"auth","token":"..."}
        {
            buffer.clear();
            co_await ws_.async_read(buffer, net::use_awaitable);
            auto auth_text = beast::buffers_to_string(buffer.data());

            nlohmann::json auth_j;
            try {
                auth_j = nlohmann::json::parse(auth_text);
            } catch (...) {
                co_return;
            }

            if (auth_j.value("type", std::string{}) != "auth") {
                co_return;
            }

            auto token = auth_j.value("token", std::string{});
            auto user  = jwt_->verify(token);
            if (!user) {
                nlohmann::json err;
                err["type"] = "error";
                err["msg"]  = "invalid token";
                auto s = err.dump();
                co_await ws_.async_write(net::buffer(s), net::use_awaitable);
                co_return;
            }

            user_id_ = static_cast<uint64_t>(user->uid);
            registry_->insert(user_id_, self);
            co_await redis_->set_presence(user_id_, server_id_);
        }

        // 3. Flush offline messages
        co_await flush_offline_queue();

        // 4. Start heartbeat supervisor on the same strand
        net::co_spawn(strand_, heartbeat_loop(), net::detached);

        // 5. Message loop
        co_await read_loop();

    } catch (const boost::system::system_error& e) {
        if (e.code() != net::error::operation_aborted &&
            e.code() != net::error::eof) {
            LOG_WARN("WebSocketSession {}: disconnected: {}", user_id_, e.what());
        }
    } catch (const std::exception& e) {
        LOG_WARN("WebSocketSession {}: error: {}", user_id_, e.what());
    }

    // Cleanup
    heartbeat_.cancel();
    if (user_id_ != 0) {
        registry_->remove(user_id_, this);
        co_await redis_->del_presence(user_id_);

        // Record last_seen in Redis (fast) and Postgres (durable)
        auto ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        co_await redis_->set_last_seen(user_id_, ts_ms);
        net::co_spawn(
            co_await net::this_coro::executor,
            pg_->execute([uid = user_id_, ts_ms](pqxx::connection& conn) {
                pqxx::work txn(conn);
                txn.exec_params(
                    "UPDATE users SET last_seen_at=to_timestamp($2::BIGINT/1000.0) WHERE id=$1",
                    static_cast<int64_t>(uid), ts_ms);
                txn.commit();
                return pqxx::result{};
            }, PgPool::RetryClass::NonRetryableWrite),
            net::detached);
    }
}

// ── read_loop ─────────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::read_loop()
{
    auto self = shared_from_this();
    beast::flat_buffer buf;

    for (;;) {
        buf.clear();
        co_await ws_.async_read(buf, net::use_awaitable);

        if (!ws_.got_text()) continue; // ignore binary frames from client

        auto text = beast::buffers_to_string(buf.data());

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(text);
        } catch (...) {
            continue;
        }

        auto type = j.value("type", std::string{});

        if (type == "ping") {
            reset_heartbeat();
            nlohmann::json pong;
            pong["type"] = "pong";
            auto s = pong.dump();
            write_queue_.push_back(s);
            if (!writing_) {
                writing_ = true;
                net::co_spawn(strand_, do_write(), net::detached);
            }
        } else if (type == "chat") {
            co_await route_message(text);
        } else if (type == "group_chat") {
            co_await route_group_message(text);
        } else if (type == "typing") {
            co_await route_typing(j);
        } else if (type == "read") {
            co_await route_read_receipt(j);
        }
    }
}

// ── route_message ─────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::route_message(std::string_view json_sv)
{
    auto self = shared_from_this();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_sv);
    } catch (...) { co_return; }

    auto conv_id_str = j.value("conv_id", std::string{});
    auto content     = j.value("content", std::string{});

    if (conv_id_str.empty() || content.empty()) co_return;

    uint64_t conv_id = 0;
    {
        auto [p, ec] = std::from_chars(conv_id_str.data(),
                                        conv_id_str.data() + conv_id_str.size(),
                                        conv_id);
        if (ec != std::errc{}) co_return;
    }

    // Look up the other member of this conversation from the DB
    uint64_t recipient_id = 0;
    try {
        auto rows = co_await pg_->execute(
            [conv_id, sender = user_id_](pqxx::connection& conn) {
                pqxx::nontransaction ntxn(conn);
                return ntxn.exec_params(
                    "SELECT user_id FROM conv_members "
                    "WHERE conv_id=$1 AND user_id != $2 LIMIT 1",
                    static_cast<int64_t>(conv_id),
                    static_cast<int64_t>(sender));
            },
            PgPool::RetryClass::ReadOnly);
        if (rows.empty()) co_return;
        recipient_id = static_cast<uint64_t>(rows[0][0].as<int64_t>());
    } catch (const std::exception& e) {
        LOG_WARN("ws route_message conv lookup conv_id={}: {}", conv_id, e.what());
        co_return;
    }

    uint64_t msg_id = static_cast<uint64_t>(snowflake_->next());
    int64_t  ts_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    OutboundMessage message;
    message.conv_id = conv_id;
    message.msg_id = msg_id;
    message.sender_id = user_id_;
    message.recipient_id = recipient_id;
    message.timestamp_ms = ts_ms;
    message.msg_type = MsgType::CHAT;
    message.content.assign(content.begin(), content.end());

    auto delivery_bytes = serialize_delivery(message);

    // Store to Cassandra async (write-behind)
    net::co_spawn(
        co_await net::this_coro::executor,
        cass_->store_message_async(
            conv_id, msg_id, user_id_, recipient_id,
            std::vector<uint8_t>(message.content.begin(), message.content.end()),
            ts_ms, static_cast<uint8_t>(MsgType::CHAT)),
        net::detached);

    std::string preview_str(message.content.begin(),
                            message.content.size() > 128
                                ? message.content.begin() + 128
                                : message.content.end());

    // Route to recipient
    auto recipient_session = registry_->lookup(recipient_id);
    if (recipient_session) {
        net::post(recipient_session->strand(),
                  [recipient_session, delivery = delivery_bytes]() mutable {
                      recipient_session->enqueue(std::move(delivery));
                  });
        // Send DELIVERED receipt back to sender
        {
            OutboundMessage receipt;
            receipt.msg_type     = MsgType::DELIVERED;
            receipt.msg_id       = msg_id;
            receipt.sender_id    = recipient_id;
            receipt.recipient_id = user_id_;
            receipt.conv_id      = conv_id;
            auto receipt_bytes   = serialize_delivery(receipt);
            write_queue_.push_back(
                [&]() -> std::string {
                    auto msg_opt = deserialize_delivery(receipt_bytes);
                    if (!msg_opt) return "";
                    nlohmann::json rj;
                    rj["type"]    = "delivered";
                    rj["msg_id"]  = std::to_string(msg_opt->msg_id);
                    rj["conv_id"] = std::to_string(msg_opt->conv_id);
                    rj["user_id"] = std::to_string(msg_opt->sender_id);
                    return rj.dump();
                }());
            if (!writing_) {
                writing_ = true;
                net::co_spawn(strand_, do_write(), net::detached);
            }
        }
    } else {
        co_await redis_->lpush("offline:" + std::to_string(recipient_id),
                               std::span<const uint8_t>(delivery_bytes));
        // Increment unread count for offline recipient
        net::co_spawn(co_await net::this_coro::executor,
                      redis_->hincrby_unread(recipient_id, conv_id),
                      net::detached);
    }

    // Update last_activity_at and preview
    net::co_spawn(
        co_await net::this_coro::executor,
        pg_->execute([conv_id, p = preview_str](pqxx::connection& conn) {
            pqxx::work txn(conn);
            txn.exec_params(
                "UPDATE conversations SET last_activity_at=NOW(), last_msg_preview=$2 WHERE id=$1",
                static_cast<int64_t>(conv_id), p);
            txn.commit();
            return pqxx::result{};
        }, PgPool::RetryClass::NonRetryableWrite),
        net::detached);
}

// ── flush_offline_queue ───────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::flush_offline_queue()
{
    auto key    = "offline:" + std::to_string(user_id_);
    auto frames = co_await redis_->lrange_and_del(key);
    for (auto& frame : frames) {
        enqueue(std::move(frame));
    }
}

// ── heartbeat_loop ────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::heartbeat_loop()
{
    auto self = shared_from_this();
    try {
        for (;;) {
            heartbeat_.expires_after(std::chrono::seconds(30));
            boost::system::error_code ec;
            co_await heartbeat_.async_wait(
                net::redirect_error(net::use_awaitable, ec));
            if (!ec) {
                LOG_WARN("WebSocketSession {}: heartbeat timeout, closing", user_id_);
                ws_.next_layer().close();
                co_return;
            }
        }
    } catch (...) {}
}

void WebSocketSession::reset_heartbeat()
{
    heartbeat_.cancel();
}

// ── enqueue ───────────────────────────────────────────────────────────────────

void WebSocketSession::enqueue(std::vector<uint8_t> frame)
{
    auto message = deserialize_delivery(frame);
    if (!message) return;

    nlohmann::json j;
    if (message->msg_type == MsgType::DELETE_NOTIFY) {
        j["type"]    = "delete_notify";
        j["msg_id"]  = std::to_string(message->msg_id);
        j["conv_id"] = std::to_string(message->conv_id);
    } else if (message->msg_type == MsgType::DELIVERED) {
        j["type"]    = "delivered";
        j["msg_id"]  = std::to_string(message->msg_id);
        j["conv_id"] = std::to_string(message->conv_id);
        j["user_id"] = std::to_string(message->sender_id);
    } else if (message->msg_type == MsgType::READ) {
        j["type"]    = "read";
        j["conv_id"] = std::to_string(message->conv_id);
        j["user_id"] = std::to_string(message->sender_id);
    } else if (message->msg_type == MsgType::TYPING) {
        j["type"]     = "typing";
        j["conv_id"]  = std::to_string(message->conv_id);
        j["user_id"]  = std::to_string(message->sender_id);
        j["is_group"] = static_cast<bool>(message->flags & kFlagIsGroup);
    } else {
        j["type"]      = "chat";
        j["msg_id"]    = std::to_string(message->msg_id);
        j["conv_id"]   = std::to_string(message->conv_id);
        j["sender_id"] = std::to_string(message->sender_id);
        j["content"]   = std::string(message->content.begin(), message->content.end());
        j["ts_ms"]     = message->timestamp_ms;
        if (message->flags & kFlagIsGroup) {
            j["group_id"] = std::to_string(message->conv_id);
        }
    }

    write_queue_.push_back(j.dump());
    if (!writing_) {
        writing_ = true;
        net::co_spawn(strand_, do_write(), net::detached);
    }
}

// ── do_write ──────────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::do_write()
{
    auto self = shared_from_this();
    try {
        while (!write_queue_.empty()) {
            auto text = std::move(write_queue_.front());
            write_queue_.pop_front();
            ws_.text(true);
            co_await ws_.async_write(net::buffer(text), net::use_awaitable);
        }
    } catch (const std::exception& e) {
        LOG_WARN("WebSocketSession {}: write error: {}", user_id_, e.what());
    }
    writing_ = false;
}

// ── route_group_message ───────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::route_group_message(std::string_view json_sv)
{
    auto self = shared_from_this();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(json_sv);
    } catch (...) { co_return; }

    auto group_id_str = j.value("group_id", std::string{});
    auto content      = j.value("content", std::string{});
    if (group_id_str.empty() || content.empty()) co_return;

    uint64_t group_id = 0;
    {
        auto [p, ec] = std::from_chars(group_id_str.data(),
                                        group_id_str.data() + group_id_str.size(),
                                        group_id);
        if (ec != std::errc{}) co_return;
    }

    uint64_t msg_id = static_cast<uint64_t>(snowflake_->next());
    int64_t  ts_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    OutboundMessage message;
    message.conv_id      = group_id;
    message.msg_id       = msg_id;
    message.sender_id    = user_id_;
    message.recipient_id = group_id;
    message.timestamp_ms = ts_ms;
    message.msg_type     = MsgType::CHAT;
    message.flags        = kFlagIsGroup;
    message.content.assign(content.begin(), content.end());

    auto delivery_bytes = serialize_delivery(message);

    // Persist async
    net::co_spawn(
        co_await net::this_coro::executor,
        cass_->store_message_async(
            group_id, msg_id, user_id_, group_id,
            std::vector<uint8_t>(message.content.begin(), message.content.end()),
            ts_ms, static_cast<uint8_t>(MsgType::CHAT)),
        net::detached);

    // Resolve members: Redis first, then Postgres
    auto members = co_await redis_->smembers_group(group_id);
    if (members.empty()) {
        members = co_await load_group_members_from_pg(group_id);
    }

    std::string group_preview(message.content.begin(),
                              message.content.size() > 128
                                  ? message.content.begin() + 128
                                  : message.content.end());

    for (auto member_id : members) {
        if (member_id == user_id_) continue;
        auto session = registry_->lookup(member_id);
        if (session) {
            net::post(session->strand(),
                      [session, delivery = delivery_bytes]() mutable {
                          session->enqueue(std::move(delivery));
                      });
        } else {
            co_await redis_->lpush("offline:" + std::to_string(member_id),
                                   std::span<const uint8_t>(delivery_bytes));
            // Increment unread for offline group member
            net::co_spawn(co_await net::this_coro::executor,
                          redis_->hincrby_unread(member_id, group_id),
                          net::detached);
        }
    }

    // Update last_activity_at and preview on groups table
    net::co_spawn(
        co_await net::this_coro::executor,
        pg_->execute([group_id, p = group_preview](pqxx::connection& conn) {
            pqxx::work txn(conn);
            txn.exec_params(
                "UPDATE groups SET last_activity_at=NOW(), last_msg_preview=$2 WHERE id=$1",
                static_cast<int64_t>(group_id), p);
            txn.commit();
            return pqxx::result{};
        }, PgPool::RetryClass::NonRetryableWrite),
        net::detached);
}

// ── load_group_members_from_pg ────────────────────────────────────────────────

net::awaitable<std::vector<uint64_t>>
WebSocketSession::load_group_members_from_pg(uint64_t group_id)
{
    try {
        auto rows = co_await pg_->execute(
            [group_id](pqxx::connection& conn) {
                pqxx::nontransaction ntxn(conn);
                return ntxn.exec_params(
                    "SELECT user_id FROM group_members WHERE group_id=$1",
                    static_cast<int64_t>(group_id));
            },
            PgPool::RetryClass::ReadOnly);

        std::vector<uint64_t> members;
        members.reserve(rows.size());
        for (const auto& row : rows) {
            members.push_back(static_cast<uint64_t>(row[0].as<int64_t>()));
        }
        co_return members;
    } catch (const std::exception& e) {
        LOG_WARN("ws load_group_members_from_pg group_id={}: {}", group_id, e.what());
        co_return std::vector<uint64_t>{};
    }
}

// ── route_typing ──────────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::route_typing(const nlohmann::json& j)
{
    auto self = shared_from_this();

    auto conv_id_str = j.value("conv_id", std::string{});
    bool is_group    = j.value("is_group", false);
    if (conv_id_str.empty()) co_return;

    uint64_t conv_id = 0;
    {
        auto [p, ec] = std::from_chars(conv_id_str.data(),
                                        conv_id_str.data() + conv_id_str.size(),
                                        conv_id);
        if (ec != std::errc{}) co_return;
    }

    OutboundMessage typing_msg;
    typing_msg.msg_type     = MsgType::TYPING;
    typing_msg.conv_id      = conv_id;
    typing_msg.sender_id    = user_id_;
    typing_msg.recipient_id = conv_id;
    typing_msg.flags        = is_group ? kFlagIsGroup : 0;
    auto typing_bytes = serialize_delivery(typing_msg);

    if (!is_group) {
        // DM: look up the other member and relay if online
        try {
            auto rows = co_await pg_->execute(
                [conv_id, sender = user_id_](pqxx::connection& conn) {
                    pqxx::nontransaction ntxn(conn);
                    return ntxn.exec_params(
                        "SELECT user_id FROM conv_members "
                        "WHERE conv_id=$1 AND user_id != $2 LIMIT 1",
                        static_cast<int64_t>(conv_id),
                        static_cast<int64_t>(sender));
                },
                PgPool::RetryClass::ReadOnly);
            if (!rows.empty()) {
                auto peer_id = static_cast<uint64_t>(rows[0][0].as<int64_t>());
                auto sess    = registry_->lookup(peer_id);
                if (sess) {
                    net::post(sess->strand(),
                              [sess, tb = typing_bytes]() mutable {
                                  sess->enqueue(std::move(tb));
                              });
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("ws route_typing DM conv_id={}: {}", conv_id, e.what());
        }
    } else {
        // Group: fan-out to all online members except sender
        auto members = co_await redis_->smembers_group(conv_id);
        if (members.empty()) {
            members = co_await load_group_members_from_pg(conv_id);
        }
        for (auto member_id : members) {
            if (member_id == user_id_) continue;
            auto sess = registry_->lookup(member_id);
            if (sess) {
                net::post(sess->strand(),
                          [sess, tb = typing_bytes]() mutable {
                              sess->enqueue(std::move(tb));
                          });
            }
            // No offline queue for typing indicators
        }
    }
}

// ── route_read_receipt ────────────────────────────────────────────────────────

net::awaitable<void> WebSocketSession::route_read_receipt(const nlohmann::json& j)
{
    auto self = shared_from_this();

    auto conv_id_str = j.value("conv_id", std::string{});
    bool is_group    = j.value("is_group", false);
    if (conv_id_str.empty()) co_return;

    uint64_t conv_id = 0;
    {
        auto [p, ec] = std::from_chars(conv_id_str.data(),
                                        conv_id_str.data() + conv_id_str.size(),
                                        conv_id);
        if (ec != std::errc{}) co_return;
    }

    // Clear unread counter in Redis
    co_await redis_->hdel_unread(user_id_, conv_id);

    // Mark as read in Postgres (async, detached)
    net::co_spawn(
        co_await net::this_coro::executor,
        pg_->execute([conv_id, uid = user_id_](pqxx::connection& conn) {
            pqxx::work txn(conn);
            txn.exec_params(
                "UPDATE message_receipts SET status=2, updated_at=NOW() "
                "WHERE conv_id=$1 AND user_id=$2 AND status<2",
                static_cast<int64_t>(conv_id),
                static_cast<int64_t>(uid));
            txn.commit();
            return pqxx::result{};
        }, PgPool::RetryClass::NonRetryableWrite),
        net::detached);

    // Build READ OutboundMessage and relay to other online members
    OutboundMessage read_msg;
    read_msg.msg_type     = MsgType::READ;
    read_msg.conv_id      = conv_id;
    read_msg.sender_id    = user_id_;
    read_msg.recipient_id = conv_id;
    read_msg.flags        = is_group ? kFlagIsGroup : 0;
    auto read_bytes = serialize_delivery(read_msg);

    if (!is_group) {
        try {
            auto rows = co_await pg_->execute(
                [conv_id, sender = user_id_](pqxx::connection& conn) {
                    pqxx::nontransaction ntxn(conn);
                    return ntxn.exec_params(
                        "SELECT user_id FROM conv_members "
                        "WHERE conv_id=$1 AND user_id != $2 LIMIT 1",
                        static_cast<int64_t>(conv_id),
                        static_cast<int64_t>(sender));
                },
                PgPool::RetryClass::ReadOnly);
            if (!rows.empty()) {
                auto peer_id = static_cast<uint64_t>(rows[0][0].as<int64_t>());
                auto sess    = registry_->lookup(peer_id);
                if (sess) {
                    net::post(sess->strand(),
                              [sess, rb = read_bytes]() mutable {
                                  sess->enqueue(std::move(rb));
                              });
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("ws route_read_receipt DM conv_id={}: {}", conv_id, e.what());
        }
    } else {
        auto members = co_await redis_->smembers_group(conv_id);
        if (members.empty()) {
            members = co_await load_group_members_from_pg(conv_id);
        }
        for (auto member_id : members) {
            if (member_id == user_id_) continue;
            auto sess = registry_->lookup(member_id);
            if (sess) {
                net::post(sess->strand(),
                          [sess, rb = read_bytes]() mutable {
                              sess->enqueue(std::move(rb));
                          });
            }
        }
    }
}

} // namespace Loomic
