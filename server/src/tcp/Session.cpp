#include "LoomicServer/tcp/Session.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <chrono>
#include <cstring>
#include <string>
#include <string_view>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>

#include <pqxx/pqxx>

namespace Loomic {

namespace net = boost::asio;

// ── File-local helpers ─────────────────────────────────────────────────────────

static net::awaitable<void> update_conv_activity(std::shared_ptr<PgPool> pg,
                                                  uint64_t conv_id,
                                                  bool is_group,
                                                  std::string preview)
{
    try {
        co_await pg->execute(
            [conv_id, is_group, p = std::move(preview)](pqxx::connection& conn) {
                pqxx::work txn(conn);
                if (is_group) {
                    txn.exec_params(
                        "UPDATE groups SET last_activity_at=NOW(), last_msg_preview=$2 WHERE id=$1",
                        static_cast<int64_t>(conv_id), p);
                } else {
                    txn.exec_params(
                        "UPDATE conversations SET last_activity_at=NOW(), last_msg_preview=$2 WHERE id=$1",
                        static_cast<int64_t>(conv_id), p);
                }
                txn.commit();
                return pqxx::result{};
            },
            PgPool::RetryClass::NonRetryableWrite);
    } catch (const std::exception& e) {
        LOG_WARN("update_conv_activity conv_id={}: {}", conv_id, e.what());
    }
}

// ── TokenBucket ───────────────────────────────────────────────────────────────

bool TokenBucket::consume()
{
    using namespace std::chrono;
    auto   now     = steady_clock::now();
    double elapsed = duration<double>(now - last_refill).count();
    last_refill    = now;
    tokens         = std::min(max_tokens, tokens + elapsed * refill_rate);
    if (tokens >= 1.0) {
        tokens -= 1.0;
        return true;
    }
    return false;
}

// ── Session ───────────────────────────────────────────────────────────────────

Session::Session(SslStream                        socket,
                 std::shared_ptr<SessionRegistry> registry,
                 std::shared_ptr<JwtService>      jwt,
                 std::shared_ptr<RedisClient>     redis,
                 std::shared_ptr<CassandraClient> cass,
                 std::shared_ptr<SnowflakeGen>    snowflake,
                 std::shared_ptr<PgPool>          pg,
                 std::string                      server_id)
    : socket_(std::move(socket))
    , strand_(net::make_strand(socket_.get_executor()))
    , heartbeat_(strand_)
    , registry_(std::move(registry))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
    , cass_(std::move(cass))
    , snowflake_(std::move(snowflake))
    , pg_(std::move(pg))
    , server_id_(std::move(server_id))
{}

net::awaitable<void> Session::run()
{
    auto self = shared_from_this();

    try {
        // ── Step 1: AUTH frame ────────────────────────────────────────────
        auto auth_hdr = co_await read_frame_header(socket_);
        if (static_cast<MsgType>(auth_hdr.msg_type) != MsgType::AUTH) {
            co_return;
        }
        if (auth_hdr.payload_len > 4096) {
            co_return;
        }

        auto auth_payload = co_await read_frame_payload(socket_, auth_hdr.payload_len);
        std::string token(auth_payload.begin(), auth_payload.end());

        auto user = jwt_->verify(token);
        if (!user) {
            FrameHeader err{};
            err.msg_type = static_cast<uint8_t>(MsgType::ERROR);
            co_await write_frame(socket_, err, {});
            co_return;
        }
        user_id_ = static_cast<uint64_t>(user->uid);
        registry_->insert(user_id_, self);
        co_await redis_->set_presence(user_id_, server_id_);

        // ── Step 2: ACK auth so the client knows auth is complete ─────────
        {
            FrameHeader ack{};
            ack.msg_type = static_cast<uint8_t>(MsgType::PONG);
            co_await write_frame(socket_, ack, {});
        }

        // ── Step 3: deliver any offline messages ──────────────────────────
        co_await flush_offline_queue();

        // ── Step 4: start heartbeat supervisor ────────────────────────────
        net::co_spawn(strand_, heartbeat_loop(), net::detached);

        // ── Step 5: message loop ──────────────────────────────────────────
        for (;;) {
            auto hdr  = co_await read_frame_header(socket_);
            auto type = static_cast<MsgType>(hdr.msg_type);

            if (type == MsgType::PING) {
                reset_heartbeat();
                co_await redis_->refresh_presence(user_id_);
                FrameHeader pong{};
                pong.msg_type = static_cast<uint8_t>(MsgType::PONG);
                co_await write_frame(socket_, pong, {});

            } else if (type == MsgType::CHAT) {
                if (hdr.payload_len > (1u << 20)) {
                    co_return;
                }
                auto payload = co_await read_frame_payload(socket_, hdr.payload_len);

                if (rate_limiter_.consume()) {
                    co_await route_message(hdr, std::move(payload));
                } else {
                    FrameHeader err{};
                    err.msg_type = static_cast<uint8_t>(MsgType::ERROR);
                    co_await write_frame(socket_, err, {});
                }

            } else if (type == MsgType::TYPING) {
                // Relay ephemeral typing indicator — no persistence, no offline queue
                uint64_t target_id = hdr.recipient_id;
                bool is_group = (hdr.flags & kFlagIsGroup) != 0;
                if (!is_group) {
                    auto target_sess = registry_->lookup(target_id);
                    if (target_sess) {
                        OutboundMessage typing_msg;
                        typing_msg.msg_type     = MsgType::TYPING;
                        typing_msg.conv_id      = target_id;
                        typing_msg.sender_id    = user_id_;
                        typing_msg.recipient_id = target_id;
                        typing_msg.flags        = 0;
                        auto typing_bytes = serialize_delivery(typing_msg);
                        net::post(target_sess->strand(),
                                  [target_sess, tb = std::move(typing_bytes)]() mutable {
                                      target_sess->enqueue(std::move(tb));
                                  });
                    }
                } else {
                    // Fan-out typing to group members
                    auto members = co_await redis_->smembers_group(target_id);
                    if (members.empty()) {
                        members = co_await load_group_members_from_pg(target_id);
                    }
                    OutboundMessage typing_msg;
                    typing_msg.msg_type     = MsgType::TYPING;
                    typing_msg.conv_id      = target_id;
                    typing_msg.sender_id    = user_id_;
                    typing_msg.recipient_id = target_id;
                    typing_msg.flags        = kFlagIsGroup;
                    auto typing_bytes = serialize_delivery(typing_msg);
                    for (auto member_id : members) {
                        if (member_id == user_id_) continue;
                        auto ms = registry_->lookup(member_id);
                        if (ms) {
                            net::post(ms->strand(),
                                      [ms, tb = typing_bytes]() mutable {
                                          ms->enqueue(std::move(tb));
                                      });
                        }
                    }
                }
            } else {
                if (hdr.payload_len > 0 && hdr.payload_len <= (1u << 20)) {
                    co_await read_frame_payload(socket_, hdr.payload_len);
                }
            }
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != net::error::operation_aborted &&
            e.code() != net::error::eof) {
            LOG_WARN("Session {}: disconnected: {}", user_id_, e.what());
        }
    } catch (const std::exception& e) {
        LOG_WARN("Session {}: error: {}", user_id_, e.what());
    }

    // ── Cleanup ───────────────────────────────────────────────────────────
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

net::awaitable<void> Session::flush_offline_queue()
{
    auto self   = shared_from_this();
    auto key    = "offline:" + std::to_string(user_id_);
    auto frames = co_await redis_->lrange_and_del(key);
    for (auto& frame : frames) {
        enqueue(std::move(frame));
    }
}

net::awaitable<void> Session::heartbeat_loop()
{
    auto self = shared_from_this();
    try {
        for (;;) {
            heartbeat_.expires_after(std::chrono::seconds(30));
            boost::system::error_code ec;
            co_await heartbeat_.async_wait(
                net::redirect_error(net::use_awaitable, ec));
            if (!ec) {
                LOG_WARN("Session {}: heartbeat timeout, closing connection", user_id_);
                boost::system::error_code close_ec;
                socket_.lowest_layer().close(close_ec);
                co_return;
            }
        }
    } catch (...) {}
}

void Session::reset_heartbeat()
{
    heartbeat_.cancel();
}

net::awaitable<void> Session::route_message(const FrameHeader& hdr,
                                             std::vector<uint8_t> payload)
{
    auto self = shared_from_this();

    if (hdr.flags & kFlagIsGroup) {
        // ── Group fan-out ────────────────────────────────────────────────────
        uint64_t group_id = hdr.recipient_id;
        uint64_t msg_id   = static_cast<uint64_t>(snowflake_->next());
        int64_t  ts_ms    = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        OutboundMessage message;
        message.conv_id      = group_id;
        message.msg_id       = msg_id;
        message.sender_id    = user_id_;
        message.recipient_id = group_id;
        message.timestamp_ms = ts_ms;
        message.msg_type     = MsgType::CHAT;
        message.flags        = kFlagIsGroup;
        message.content      = std::move(payload);

        auto delivery_bytes = serialize_delivery(message);

        // Resolve group members: Redis first, fall back to Postgres
        auto members = co_await redis_->smembers_group(group_id);
        if (members.empty()) {
            members = co_await load_group_members_from_pg(group_id);
        }

        std::string preview_str(message.content.begin(),
                                message.content.size() > 128 ? message.content.begin() + 128
                                                               : message.content.end());

        for (auto member_id : members) {
            if (member_id == user_id_) continue;  // skip sender
            auto session = registry_->lookup(member_id);
            if (session) {
                net::post(session->strand(),
                          [session, delivery = delivery_bytes]() mutable {
                              session->enqueue(std::move(delivery));
                          });
                // Send DELIVERED receipt back to original sender
                {
                    OutboundMessage receipt;
                    receipt.msg_type     = MsgType::DELIVERED;
                    receipt.msg_id       = msg_id;
                    receipt.sender_id    = member_id;
                    receipt.recipient_id = user_id_;
                    receipt.conv_id      = group_id;
                    receipt.flags        = kFlagIsGroup;
                    auto receipt_bytes   = serialize_delivery(receipt);
                    auto sender_sess     = registry_->lookup(user_id_);
                    if (sender_sess) {
                        net::post(sender_sess->strand(),
                                  [sender_sess, rf = std::move(receipt_bytes)]() mutable {
                                      sender_sess->enqueue(std::move(rf));
                                  });
                    }
                }
            } else {
                co_await redis_->lpush("offline:" + std::to_string(member_id),
                                       std::span<const uint8_t>(delivery_bytes));
                // Increment unread count for offline recipient
                net::co_spawn(co_await net::this_coro::executor,
                              redis_->hincrby_unread(member_id, group_id),
                              net::detached);
            }
        }

        // Async write-behind to Cassandra (conv_id == group_id for groups)
        net::co_spawn(
            co_await net::this_coro::executor,
            cass_->store_message_async(
                group_id, msg_id, user_id_, group_id,
                std::vector<uint8_t>(message.content.begin(), message.content.end()),
                ts_ms, static_cast<uint8_t>(MsgType::CHAT)),
            net::detached);

        // Update last_activity_at and preview on groups table
        net::co_spawn(co_await net::this_coro::executor,
                      update_conv_activity(pg_, group_id, true, preview_str),
                      net::detached);

    } else {
        // ── Direct message (existing logic) ──────────────────────────────────
        uint64_t conv_id = std::min(user_id_, hdr.recipient_id);
        uint64_t msg_id  = static_cast<uint64_t>(snowflake_->next());
        int64_t  ts_ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        OutboundMessage message;
        message.conv_id      = conv_id;
        message.msg_id       = msg_id;
        message.sender_id    = user_id_;
        message.recipient_id = hdr.recipient_id;
        message.timestamp_ms = ts_ms;
        message.msg_type     = MsgType::CHAT;
        message.flags        = 0;
        message.content      = std::move(payload);

        auto delivery_bytes = serialize_delivery(message);

        std::string preview_str_dm(message.content.begin(),
                                   message.content.size() > 128
                                       ? message.content.begin() + 128
                                       : message.content.end());

        auto recipient = registry_->lookup(hdr.recipient_id);
        if (recipient) {
            net::post(recipient->strand(),
                      [recipient, delivery = delivery_bytes]() mutable {
                          recipient->enqueue(std::move(delivery));
                      });
            // Send DELIVERED receipt back to sender
            {
                OutboundMessage receipt;
                receipt.msg_type     = MsgType::DELIVERED;
                receipt.msg_id       = msg_id;
                receipt.sender_id    = hdr.recipient_id;
                receipt.recipient_id = user_id_;
                receipt.conv_id      = conv_id;
                receipt.flags        = 0;
                auto receipt_bytes   = serialize_delivery(receipt);
                auto sender_sess     = registry_->lookup(user_id_);
                if (sender_sess) {
                    net::post(sender_sess->strand(),
                              [sender_sess, rf = std::move(receipt_bytes)]() mutable {
                                  sender_sess->enqueue(std::move(rf));
                              });
                }
            }
        } else {
            co_await redis_->lpush("offline:" + std::to_string(hdr.recipient_id),
                                   std::span<const uint8_t>(delivery_bytes));
            // Increment unread count for offline recipient
            net::co_spawn(co_await net::this_coro::executor,
                          redis_->hincrby_unread(hdr.recipient_id, conv_id),
                          net::detached);
        }

        net::co_spawn(
            co_await net::this_coro::executor,
            cass_->store_message_async(
                conv_id, msg_id, user_id_, hdr.recipient_id,
                std::vector<uint8_t>(message.content.begin(), message.content.end()),
                ts_ms, static_cast<uint8_t>(MsgType::CHAT)),
            net::detached);

        net::co_spawn(
            co_await net::this_coro::executor,
            upsert_conv_members(conv_id, user_id_, hdr.recipient_id),
            net::detached);

        // Update last_activity_at and preview on conversations table
        net::co_spawn(co_await net::this_coro::executor,
                      update_conv_activity(pg_, conv_id, false, preview_str_dm),
                      net::detached);
    }
}

net::awaitable<std::vector<uint64_t>> Session::load_group_members_from_pg(uint64_t group_id)
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
        LOG_WARN("load_group_members_from_pg group_id={}: {}", group_id, e.what());
        co_return std::vector<uint64_t>{};
    }
}

net::awaitable<void> Session::upsert_conv_members(uint64_t conv_id,
                                                   uint64_t user_a,
                                                   uint64_t user_b)
{
    try {
        co_await pg_->execute(
            [conv_id, user_a, user_b](pqxx::connection& conn) {
                pqxx::work txn(conn);
                // Ensure the conversations row exists before inserting members
                // (required by the FK constraint added in V5)
                txn.exec_params(
                    "INSERT INTO conversations (id) VALUES ($1) ON CONFLICT DO NOTHING",
                    static_cast<int64_t>(conv_id));
                txn.exec_params(
                    "INSERT INTO conv_members (conv_id, user_id) VALUES ($1,$2),($1,$3)"
                    " ON CONFLICT DO NOTHING",
                    static_cast<int64_t>(conv_id),
                    static_cast<int64_t>(user_a),
                    static_cast<int64_t>(user_b));
                txn.commit();
                return pqxx::result{};
            },
            PgPool::RetryClass::NonRetryableWrite);
    } catch (const std::exception& e) {
        LOG_WARN("upsert_conv_members conv_id={}: {}", conv_id, e.what());
    }
}

void Session::enqueue(std::vector<uint8_t> frame)
{
    auto message = deserialize_delivery(frame);
    if (!message) {
        LOG_WARN("Session {}: failed to decode queued delivery", user_id_);
        return;
    }

    std::vector<uint8_t> wire_frame;
    if (message->msg_type == MsgType::DELIVERED ||
        message->msg_type == MsgType::READ      ||
        message->msg_type == MsgType::TYPING) {
        // These are header-only frames with no proto payload
        FrameHeader hdr{};
        hdr.payload_len  = 0;
        hdr.msg_type     = static_cast<uint8_t>(message->msg_type);
        hdr.flags        = message->flags;
        hdr.msg_id       = message->msg_id;
        hdr.sender_id    = message->sender_id;
        hdr.recipient_id = message->recipient_id;
        wire_frame.resize(sizeof(FrameHeader));
        std::memcpy(wire_frame.data(), &hdr, sizeof(FrameHeader));
    } else {
        wire_frame = build_chat_frame(*message);
    }

    write_queue_.push_back(std::move(wire_frame));
    if (!writing_) {
        writing_ = true;
        net::co_spawn(strand_, do_write(), net::detached);
    }
}

net::awaitable<void> Session::do_write()
{
    auto self = shared_from_this();
    try {
        while (!write_queue_.empty()) {
            auto frame = std::move(write_queue_.front());
            write_queue_.pop_front();
            co_await net::async_write(socket_, net::buffer(frame),
                                      net::use_awaitable);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Session {}: write error: {}", user_id_, e.what());
    }
    writing_ = false;
}

} // namespace Loomic
