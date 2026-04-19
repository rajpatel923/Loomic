#include "LoomicServer/http/UsersHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <algorithm>
#include <charconv>
#include <string>
#include <string_view>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

namespace {

std::string_view query_param(std::string_view target, std::string_view key)
{
    auto qpos = target.find('?');
    if (qpos == std::string_view::npos) return {};
    auto qs = target.substr(qpos + 1);
    while (!qs.empty()) {
        auto amp  = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        auto eq   = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key) {
            return pair.substr(eq + 1);
        }
        if (amp == std::string_view::npos) break;
        qs = qs.substr(amp + 1);
    }
    return {};
}

} // namespace

UsersHandler::UsersHandler(std::shared_ptr<PgPool>      pg,
                            std::shared_ptr<JwtService>  jwt,
                            std::shared_ptr<RedisClient> redis)
    : pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
{}

void UsersHandler::register_routes(HttpServer& http)
{
    auto pg    = pg_;
    auto jwt   = jwt_;
    auto redis = redis_;

    // ── GET /users/search?q=<prefix>&limit=<n> ────────────────────────────────
    http.add_route(http::verb::get, "/users/search",
        [pg, jwt](const Request& req, const PathParams&) -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            auto target = std::string_view(req.target());
            auto q      = std::string(query_param(target, "q"));
            if (q.size() < 2) {
                co_return make_error(http::status::bad_request, "q must be at least 2 characters");
            }

            uint32_t limit = 10;
            auto limit_sv  = query_param(target, "limit");
            if (!limit_sv.empty()) {
                uint32_t parsed = 10;
                auto [p, ec] = std::from_chars(limit_sv.data(),
                                               limit_sv.data() + limit_sv.size(),
                                               parsed);
                if (ec == std::errc{}) limit = std::clamp(parsed, 1u, 20u);
            }

            try {
                auto rows = co_await pg->execute(
                    [q, limit](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id, username FROM users"
                            " WHERE username ILIKE $1 || '%' LIMIT $2",
                            q, static_cast<int>(limit));
                    },
                    PgPool::RetryClass::ReadOnly);

                nlohmann::json arr = nlohmann::json::array();
                for (const auto& row : rows) {
                    nlohmann::json item;
                    item["id"]       = std::to_string(row[0].as<int64_t>());
                    item["username"] = row[1].as<std::string>();
                    arr.push_back(std::move(item));
                }
                nlohmann::json resp;
                resp["users"] = std::move(arr);
                co_return make_json(http::status::ok, resp.dump());
            } catch (const std::exception& e) {
                LOG_ERROR("users/search: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });

    // ── GET /users/{id} ───────────────────────────────────────────────────────
    http.add_route(http::verb::get, "/users/{id}",
        [pg](const Request& /*req*/, const PathParams& params) -> net::awaitable<Response> {
            uint64_t uid = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), uid);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            try {
                auto rows = co_await pg->execute(
                    [uid](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id, username, bio, avatar_url FROM users WHERE id=$1",
                            static_cast<int64_t>(uid));
                    },
                    PgPool::RetryClass::ReadOnly);

                if (rows.empty()) co_return make_error(http::status::not_found, "user not found");

                nlohmann::json resp;
                resp["id"]         = std::to_string(rows[0][0].as<int64_t>());
                resp["username"]   = rows[0][1].as<std::string>();
                resp["bio"]        = rows[0][2].is_null() ? "" : rows[0][2].as<std::string>();
                resp["avatar_url"] = rows[0][3].is_null() ? "" : rows[0][3].as<std::string>();
                co_return make_json(http::status::ok, resp.dump());
            } catch (const std::exception& e) {
                LOG_ERROR("GET /users/{}: {}", uid, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });

    // ── GET /users/{id}/presence ──────────────────────────────────────────────
    http.add_route(http::verb::get, "/users/{id}/presence",
        [jwt, redis](const Request& req, const PathParams& params) -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t uid = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), uid);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            auto presence = co_await redis->get_presence(uid);

            nlohmann::json resp;
            resp["user_id"] = std::to_string(uid);
            if (presence) {
                resp["online"]    = true;
                resp["server_id"] = *presence;
            } else {
                resp["online"]    = false;
                resp["server_id"] = nullptr;
            }
            co_return make_json(http::status::ok, resp.dump());
        });

    // ── PATCH /users/{id} ─────────────────────────────────────────────────────
    http.add_route(http::verb::patch, "/users/{id}",
        [pg, jwt](const Request& req, const PathParams& params) -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t uid = 0;
            {
                auto& s = params.at("id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), uid);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid id");
            }

            if (uid != static_cast<uint64_t>(user->uid)) {
                co_return make_error(http::status::forbidden, "cannot modify another user");
            }

            nlohmann::json body;
            try { body = nlohmann::json::parse(req.body()); }
            catch (...) { co_return make_error(http::status::bad_request, "invalid JSON"); }

            // Only update fields present in the request
            std::optional<std::string> bio;
            std::optional<std::string> avatar_url;
            if (body.contains("bio")        && body["bio"].is_string())
                bio        = body["bio"].get<std::string>();
            if (body.contains("avatar_url") && body["avatar_url"].is_string())
                avatar_url = body["avatar_url"].get<std::string>();

            try {
                co_await pg->execute(
                    [uid, bio, avatar_url](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        // Use COALESCE so absent fields are left unchanged
                        txn.exec_params(
                            "UPDATE users SET"
                            " bio        = COALESCE($2, bio),"
                            " avatar_url = COALESCE($3, avatar_url)"
                            " WHERE id = $1",
                            static_cast<int64_t>(uid),
                            bio,        // std::optional<std::string> — NULL if absent
                            avatar_url);// std::optional<std::string> — NULL if absent
                        txn.commit();
                        return pqxx::result{};
                    },
                    PgPool::RetryClass::NonRetryableWrite);

                co_return make_json(http::status::ok, R"({"updated":true})");
            } catch (const std::exception& e) {
                LOG_ERROR("PATCH /users/{}: {}", uid, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }
        });
}

} // namespace Loomic
