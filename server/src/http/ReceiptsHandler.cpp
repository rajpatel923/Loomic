#include "LoomicServer/http/ReceiptsHandler.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/tcp/ISession.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/tcp/frame.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <charconv>
#include <string>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

ReceiptsHandler::ReceiptsHandler(std::shared_ptr<PgPool>          pg,
                                  std::shared_ptr<JwtService>      jwt,
                                  std::shared_ptr<RedisClient>     redis,
                                  std::shared_ptr<SessionRegistry> registry)
    : pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
    , registry_(std::move(registry))
{}

void ReceiptsHandler::register_routes(HttpServer& http_server)
{
    // POST /conversations/{id}/read
    http_server.add_route(http::verb::post, "/conversations/{id}/read",
        [this](const Request& req, const PathParams& params) -> net::awaitable<Response>
        {
            // 1. Auth
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
            if (!user) {
                co_return make_error(http::status::forbidden, "missing or invalid token");
            }

            // 2. Parse conv_id
            auto id_it = params.find("id");
            if (id_it == params.end()) {
                co_return make_error(http::status::bad_request, "missing conv_id");
            }
            uint64_t conv_id = 0;
            {
                auto [p, ec] = std::from_chars(id_it->second.data(),
                                               id_it->second.data() + id_it->second.size(),
                                               conv_id);
                if (ec != std::errc{}) {
                    co_return make_error(http::status::bad_request, "invalid conv_id");
                }
            }

            auto user_id = static_cast<uint64_t>(user->uid);

            // 3. Verify membership (DM then group)
            try {
                bool is_member = false;
                auto dm_row = co_await pg_->execute(
                    [conv_id, user_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT 1 FROM conv_members WHERE conv_id=$1 AND user_id=$2",
                            static_cast<int64_t>(conv_id),
                            static_cast<int64_t>(user_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                if (!dm_row.empty()) is_member = true;

                if (!is_member) {
                    auto grp_row = co_await pg_->execute(
                        [conv_id, user_id](pqxx::connection& conn) {
                            pqxx::nontransaction ntxn(conn);
                            return ntxn.exec_params(
                                "SELECT 1 FROM group_members WHERE group_id=$1 AND user_id=$2",
                                static_cast<int64_t>(conv_id),
                                static_cast<int64_t>(user_id));
                        },
                        PgPool::RetryClass::ReadOnly);
                    if (!grp_row.empty()) is_member = true;
                }

                if (!is_member) {
                    co_return make_error(http::status::forbidden, "not a member");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("ReceiptsHandler membership check conv_id={}: {}", conv_id, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            // 4. Clear unread in Redis
            co_await redis_->hdel_unread(user_id, conv_id);

            // 5. Mark receipts as read in Postgres (detached)
            net::co_spawn(
                co_await net::this_coro::executor,
                pg_->execute([conv_id, user_id](pqxx::connection& conn) {
                    pqxx::work txn(conn);
                    txn.exec_params(
                        "UPDATE message_receipts SET status=2, updated_at=NOW() "
                        "WHERE conv_id=$1 AND user_id=$2 AND status<2",
                        static_cast<int64_t>(conv_id),
                        static_cast<int64_t>(user_id));
                    txn.commit();
                    return pqxx::result{};
                }, PgPool::RetryClass::NonRetryableWrite),
                net::detached);

            // 6. Get other members and relay READ frame to online ones
            try {
                std::vector<uint64_t> other_members;
                auto dm_rows = co_await pg_->execute(
                    [conv_id, user_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT user_id FROM conv_members WHERE conv_id=$1 AND user_id != $2",
                            static_cast<int64_t>(conv_id),
                            static_cast<int64_t>(user_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                for (const auto& row : dm_rows) {
                    other_members.push_back(static_cast<uint64_t>(row[0].as<int64_t>()));
                }
                if (other_members.empty()) {
                    // Try group members
                    auto grp_rows = co_await pg_->execute(
                        [conv_id, user_id](pqxx::connection& conn) {
                            pqxx::nontransaction ntxn(conn);
                            return ntxn.exec_params(
                                "SELECT user_id FROM group_members WHERE group_id=$1 AND user_id != $2",
                                static_cast<int64_t>(conv_id),
                                static_cast<int64_t>(user_id));
                        },
                        PgPool::RetryClass::ReadOnly);
                    for (const auto& row : grp_rows) {
                        other_members.push_back(static_cast<uint64_t>(row[0].as<int64_t>()));
                    }
                }

                OutboundMessage read_msg;
                read_msg.msg_type     = MsgType::READ;
                read_msg.conv_id      = conv_id;
                read_msg.sender_id    = user_id;
                read_msg.recipient_id = conv_id;
                auto read_bytes = serialize_delivery(read_msg);

                for (auto member_id : other_members) {
                    auto sess = registry_->lookup(member_id);
                    if (sess) {
                        net::post(sess->strand(),
                                  [sess, rb = read_bytes]() mutable {
                                      sess->enqueue(std::move(rb));
                                  });
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("ReceiptsHandler relay conv_id={}: {}", conv_id, e.what());
            }

            co_return make_json(http::status::ok, R"({"ok":true})");
        });
}

} // namespace Loomic
