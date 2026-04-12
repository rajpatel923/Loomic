#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/PasswordService.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>

#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <pqxx/pqxx>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

namespace {

std::string generate_refresh_token()
{
    unsigned char bytes[32];
    RAND_bytes(bytes, sizeof(bytes));
    std::string hex;
    hex.reserve(64);
    char buf[3];
    for (auto b : bytes) {
        std::snprintf(buf, sizeof(buf), "%02x", b);
        hex += buf;
    }
    return hex;
}

} // anonymous namespace

// ── Public API ────────────────────────────────────────────────────────────────

std::optional<AuthUser> require_auth(std::string_view authorization_header,
                                     const JwtService& jwt)
{
    constexpr std::string_view prefix = "Bearer ";
    if (authorization_header.size() <= prefix.size() ||
        authorization_header.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }
    return jwt.verify(authorization_header.substr(prefix.size()));
}

void register_auth_routes(HttpServer& server,
                          std::shared_ptr<PgPool>          pg,
                          std::shared_ptr<SnowflakeGen>    snowflake,
                          std::shared_ptr<JwtService>      jwt,
                          std::shared_ptr<PasswordService> pwd)
{
    // ── POST /auth/register ───────────────────────────────────────────────────
    server.add_route(http::verb::post, "/auth/register",
        [pg, snowflake, pwd](Request req) -> net::awaitable<Response> {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body());
            } catch (...) {
                co_return make_error(http::status::bad_request, "invalid JSON");
            }

            auto username = body.value("username", std::string{});
            auto email    = body.value("email",    std::string{});
            auto password = body.value("password", std::string{});

            if (username.empty() || email.empty() || password.empty()) {
                co_return make_error(http::status::bad_request, "missing required fields");
            }
            if (password.size() < 8) {
                co_return make_error(http::status::bad_request,
                    "password must be at least 8 characters");
            }

            // Check uniqueness
            try {
                auto r = co_await pg->execute(
                    [un = username, em = email](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id FROM users WHERE username=$1 OR email=$2",
                            un, em);
                    });
                if (!r.empty()) {
                    co_return make_error(http::status::conflict,
                        "username or email already taken");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("register uniqueness check: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            auto pw_hash = co_await pwd->hash(password);
            auto uid     = snowflake->next();

            try {
                co_await pg->execute(
                    [uid, un = username, em = email, h = pw_hash](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        auto r = txn.exec_params(
                            "INSERT INTO users (id, username, email, password_hash) "
                            "VALUES ($1, $2, $3, $4)",
                            uid, un, em, h);
                        txn.commit();
                        return r;
                    });
            } catch (const std::exception& e) {
                LOG_ERROR("register insert: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            nlohmann::json resp;
            resp["id"]       = std::to_string(uid);
            resp["username"] = username;
            co_return make_json(http::status::created, resp.dump());
        });

    // ── POST /auth/login ──────────────────────────────────────────────────────
    server.add_route(http::verb::post, "/auth/login",
        [pg, jwt, pwd](Request req) -> net::awaitable<Response> {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body());
            } catch (...) {
                co_return make_error(http::status::bad_request, "invalid JSON");
            }

            auto username = body.value("username", std::string{});
            auto password = body.value("password", std::string{});

            if (username.empty() || password.empty()) {
                co_return make_error(http::status::bad_request, "missing required fields");
            }

            pqxx::result user_row;
            try {
                user_row = co_await pg->execute(
                    [un = username](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id, password_hash FROM users WHERE username=$1", un);
                    });
            } catch (const std::exception& e) {
                LOG_ERROR("login query: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            if (user_row.empty()) {
                co_return make_error(http::status::unauthorized, "invalid credentials");
            }

            auto uid     = user_row[0][0].as<int64_t>();
            auto pw_hash = user_row[0][1].as<std::string>();

            bool ok = co_await pwd->verify(password, pw_hash);
            if (!ok) {
                co_return make_error(http::status::unauthorized, "invalid credentials");
            }

            auto access_token  = jwt->issue(uid, std::chrono::hours(24));
            auto refresh_token = generate_refresh_token();

            try {
                co_await pg->execute(
                    [uid, rt = refresh_token](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        auto r = txn.exec_params(
                            "INSERT INTO refresh_tokens (token, user_id, expires_at) "
                            "VALUES ($1, $2, NOW() + INTERVAL '30 days')",
                            rt, uid);
                        txn.commit();
                        return r;
                    });
            } catch (const std::exception& e) {
                LOG_ERROR("login insert refresh token: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            nlohmann::json resp;
            resp["access_token"]  = access_token;
            resp["refresh_token"] = refresh_token;
            resp["token_type"]    = "Bearer";
            co_return make_json(http::status::ok, resp.dump());
        });

    // ── POST /auth/refresh ────────────────────────────────────────────────────
    server.add_route(http::verb::post, "/auth/refresh",
        [pg, jwt](Request req) -> net::awaitable<Response> {
            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body());
            } catch (...) {
                co_return make_error(http::status::bad_request, "invalid JSON");
            }

            auto old_token = body.value("refresh_token", std::string{});
            if (old_token.empty()) {
                co_return make_error(http::status::bad_request, "missing refresh_token");
            }

            // Verify old token and fetch user_id
            pqxx::result token_row;
            try {
                token_row = co_await pg->execute(
                    [ot = old_token](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT user_id FROM refresh_tokens "
                            "WHERE token=$1 AND expires_at > NOW()", ot);
                    });
            } catch (const std::exception& e) {
                LOG_ERROR("refresh token query: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            if (token_row.empty()) {
                co_return make_error(http::status::unauthorized, "invalid or expired token");
            }

            auto uid         = token_row[0][0].as<int64_t>();
            auto new_refresh = generate_refresh_token();

            // Rotate: delete old, insert new (in a transaction)
            try {
                co_await pg->execute(
                    [ot = old_token, nr = new_refresh, uid](pqxx::connection& conn) {
                        pqxx::work txn(conn);
                        txn.exec_params("DELETE FROM refresh_tokens WHERE token=$1", ot);
                        auto r = txn.exec_params(
                            "INSERT INTO refresh_tokens (token, user_id, expires_at) "
                            "VALUES ($1, $2, NOW() + INTERVAL '30 days')",
                            nr, uid);
                        txn.commit();
                        return r;
                    });
            } catch (const std::exception& e) {
                LOG_ERROR("refresh token rotation: {}", e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            auto access_token = jwt->issue(uid, std::chrono::hours(24));

            nlohmann::json resp;
            resp["access_token"]  = access_token;
            resp["refresh_token"] = new_refresh;
            resp["token_type"]    = "Bearer";
            co_return make_json(http::status::ok, resp.dump());
        });
}

} // namespace Loomic
