#include "LoomicServer/http/GroupsHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <charconv>
#include <string>
#include <vector>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

GroupsHandler::GroupsHandler(std::shared_ptr<PgPool>       pg,
                              std::shared_ptr<JwtService>   jwt,
                              std::shared_ptr<SnowflakeGen> snowflake,
                              std::shared_ptr<RedisClient>  redis)
    : pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , snowflake_(std::move(snowflake))
    , redis_(std::move(redis))
{}

void GroupsHandler::register_routes(HttpServer& http)
{
    auto pg        = pg_;
    auto jwt       = jwt_;
    auto snowflake = snowflake_;
    auto redis     = redis_;

    // ── POST /groups ──────────────────────────────────────────────────────────
    http.add_route(http::verb::post, "/groups",
        [pg, jwt, snowflake, redis](const Request& req, const PathParams&)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body()); }
            catch (...) { co_return make_error(http::status::bad_request, "invalid JSON"); }

            auto name = body.value("name", std::string{});
            if (name.empty()) co_return make_error(http::status::bad_request, "name required");

            uint64_t creator_id = static_cast<uint64_t>(user->uid);
            uint64_t group_id   = static_cast<uint64_t>(snowflake->next());

            // Parse member_ids array (strings → uint64_t)
            std::vector<uint64_t> member_ids;
            if (body.contains("member_ids") && body["member_ids"].is_array()) {
                for (const auto& item : body["member_ids"]) {
                    auto s = item.get<std::string>();
                    uint64_t uid = 0;
                    auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), uid);
                    if (ec == std::errc{}) member_ids.push_back(uid);
                }
            }

            std::string created_at_str;
            try {
                auto res = co_await pg->execute(
                    [group_id, name, creator_id, member_ids](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        auto r = txn.exec_params(
                            "INSERT INTO groups (id, name, creator_id) VALUES ($1,$2,$3)"
                            " RETURNING created_at",
                            static_cast<int64_t>(group_id), name,
                            static_cast<int64_t>(creator_id));
                        // Insert creator as admin
                        txn.exec_params(
                            "INSERT INTO group_members (group_id, user_id, role) VALUES ($1,$2,'admin')",
                            static_cast<int64_t>(group_id),
                            static_cast<int64_t>(creator_id));
                        // Insert remaining members
                        for (auto uid : member_ids) {
                            txn.exec_params(
                                "INSERT INTO group_members (group_id, user_id, role)"
                                " VALUES ($1,$2,'member') ON CONFLICT DO NOTHING",
                                static_cast<int64_t>(group_id),
                                static_cast<int64_t>(uid));
                        }
                        txn.commit();
                        return r;
                    },
                    PgPool::RetryClass::NonRetryableWrite);
                if (!res.empty()) created_at_str = res[0][0].as<std::string>();
            } catch (const std::exception& e) {
                LOG_ERROR("POST /groups: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            // Populate Redis set with all members
            co_await redis->sadd_group_member(group_id, creator_id);
            for (auto uid : member_ids) {
                co_await redis->sadd_group_member(group_id, uid);
            }

            nlohmann::json resp;
            resp["group_id"]    = std::to_string(group_id);
            resp["name"]        = name;
            resp["created_at"]  = created_at_str;
            co_return make_json(http::status::created, resp.dump());
        });

    // ── GET /groups/{id} ─────────────────────────────────────────────────────
    http.add_route(http::verb::get, "/groups/{id}",
        [pg, jwt](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t group_id = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), group_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            uint64_t caller_id = static_cast<uint64_t>(user->uid);

            try {
                // Get group info
                auto grows = co_await pg->execute(
                    [group_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id, name, creator_id, created_at FROM groups WHERE id=$1",
                            static_cast<int64_t>(group_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                if (grows.empty()) co_return make_error(http::status::not_found, "group not found");

                // Get members with username and role
                auto mrows = co_await pg->execute(
                    [group_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT gm.user_id, u.username, gm.role"
                            " FROM group_members gm JOIN users u ON u.id = gm.user_id"
                            " WHERE gm.group_id=$1",
                            static_cast<int64_t>(group_id));
                    },
                    PgPool::RetryClass::ReadOnly);

                // Verify caller is a member
                bool is_member = false;
                for (const auto& row : mrows) {
                    if (static_cast<uint64_t>(row[0].as<int64_t>()) == caller_id) {
                        is_member = true; break;
                    }
                }
                if (!is_member) co_return make_error(http::status::forbidden, "not a member");

                nlohmann::json resp;
                resp["group_id"]   = std::to_string(grows[0][0].as<int64_t>());
                resp["name"]       = grows[0][1].as<std::string>();
                resp["creator_id"] = std::to_string(grows[0][2].as<int64_t>());
                resp["created_at"] = grows[0][3].as<std::string>();

                auto members_arr = nlohmann::json::array();
                for (const auto& row : mrows) {
                    nlohmann::json m;
                    m["user_id"]  = std::to_string(row[0].as<int64_t>());
                    m["username"] = row[1].as<std::string>();
                    m["role"]     = row[2].as<std::string>();
                    members_arr.push_back(std::move(m));
                }
                resp["members"] = std::move(members_arr);
                co_return make_json(http::status::ok, resp.dump());
            } catch (const std::exception& e) {
                LOG_ERROR("GET /groups/{}: {}", group_id, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });

    // ── PATCH /groups/{id} ───────────────────────────────────────────────────
    http.add_route(http::verb::patch, "/groups/{id}",
        [pg, jwt](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t group_id = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), group_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body()); }
            catch (...) { co_return make_error(http::status::bad_request, "invalid JSON"); }

            auto name = body.value("name", std::string{});
            if (name.empty()) co_return make_error(http::status::bad_request, "name required");

            uint64_t caller_id = static_cast<uint64_t>(user->uid);

            try {
                // Verify caller is admin
                auto rows = co_await pg->execute(
                    [group_id, caller_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT role FROM group_members WHERE group_id=$1 AND user_id=$2",
                            static_cast<int64_t>(group_id),
                            static_cast<int64_t>(caller_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                if (rows.empty() || rows[0][0].as<std::string>() != "admin") {
                    co_return make_error(http::status::forbidden, "admin required");
                }

                co_await pg->execute(
                    [group_id, name](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        txn.exec_params("UPDATE groups SET name=$1 WHERE id=$2",
                                        name, static_cast<int64_t>(group_id));
                        txn.commit();
                        return pqxx::result{};
                    },
                    PgPool::RetryClass::NonRetryableWrite);

                nlohmann::json resp;
                resp["group_id"] = std::to_string(group_id);
                resp["name"]     = name;
                co_return make_json(http::status::ok, resp.dump());
            } catch (const std::exception& e) {
                LOG_ERROR("PATCH /groups/{}: {}", group_id, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });

    // ── POST /groups/{id}/members ────────────────────────────────────────────
    http.add_route(http::verb::post, "/groups/{id}/members",
        [pg, jwt, redis](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t group_id = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), group_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body()); }
            catch (...) { co_return make_error(http::status::bad_request, "invalid JSON"); }

            auto uid_str = body.value("user_id", std::string{});
            if (uid_str.empty()) co_return make_error(http::status::bad_request, "user_id required");

            uint64_t new_uid = 0;
            {
                auto [p, ec] = std::from_chars(uid_str.data(), uid_str.data() + uid_str.size(), new_uid);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid user_id");
            }

            uint64_t caller_id = static_cast<uint64_t>(user->uid);

            try {
                // Verify caller is admin
                auto rows = co_await pg->execute(
                    [group_id, caller_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT role FROM group_members WHERE group_id=$1 AND user_id=$2",
                            static_cast<int64_t>(group_id),
                            static_cast<int64_t>(caller_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                if (rows.empty() || rows[0][0].as<std::string>() != "admin") {
                    co_return make_error(http::status::forbidden, "admin required");
                }

                co_await pg->execute(
                    [group_id, new_uid](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        txn.exec_params(
                            "INSERT INTO group_members (group_id, user_id, role)"
                            " VALUES ($1,$2,'member') ON CONFLICT DO NOTHING",
                            static_cast<int64_t>(group_id),
                            static_cast<int64_t>(new_uid));
                        txn.commit();
                        return pqxx::result{};
                    },
                    PgPool::RetryClass::NonRetryableWrite);

                co_await redis->sadd_group_member(group_id, new_uid);

                nlohmann::json resp;
                resp["group_id"] = std::to_string(group_id);
                resp["user_id"]  = std::to_string(new_uid);
                co_return make_json(http::status::created, resp.dump());
            } catch (const std::exception& e) {
                LOG_ERROR("POST /groups/{}/members: {}", group_id, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });

    // ── DELETE /groups/{id}/members/{uid} ────────────────────────────────────
    http.add_route(http::verb::delete_, "/groups/{id}/members/{uid}",
        [pg, jwt, redis](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t group_id = 0, target_uid = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), group_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }
            {
                auto& s = params.at("uid");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), target_uid);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid uid");
            }

            uint64_t caller_id = static_cast<uint64_t>(user->uid);

            // Allow if caller is self-removing OR is admin
            bool may_remove = (caller_id == target_uid);
            if (!may_remove) {
                try {
                    auto rows = co_await pg->execute(
                        [group_id, caller_id](pqxx::connection& conn) {
                            pqxx::nontransaction ntxn(conn);
                            return ntxn.exec_params(
                                "SELECT role FROM group_members WHERE group_id=$1 AND user_id=$2",
                                static_cast<int64_t>(group_id),
                                static_cast<int64_t>(caller_id));
                        },
                        PgPool::RetryClass::ReadOnly);
                    if (!rows.empty() && rows[0][0].as<std::string>() == "admin") {
                        may_remove = true;
                    }
                } catch (...) {}
            }
            if (!may_remove) co_return make_error(http::status::forbidden, "admin required or removing self");

            try {
                co_await pg->execute(
                    [group_id, target_uid](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        txn.exec_params(
                            "DELETE FROM group_members WHERE group_id=$1 AND user_id=$2",
                            static_cast<int64_t>(group_id),
                            static_cast<int64_t>(target_uid));
                        txn.commit();
                        return pqxx::result{};
                    },
                    PgPool::RetryClass::NonRetryableWrite);

                co_await redis->srem_group_member(group_id, target_uid);

                Response res{http::status::no_content, 11};
                res.prepare_payload();
                co_return res;
            } catch (const std::exception& e) {
                LOG_ERROR("DELETE /groups/{}/members/{}: {}", group_id, target_uid, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });
}

} // namespace Loomic
