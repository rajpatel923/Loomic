#include "LoomicServer/http/UsersHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/db/PgPool.hpp"
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

void register_users_routes(HttpServer&                 http,
                           std::shared_ptr<PgPool>     pg,
                           std::shared_ptr<JwtService> jwt)
{
    // ── GET /users/search?q=<prefix>&limit=<n> ────────────────────────────────
    http.add_route(http::verb::get, "/users/search",
        [pg, jwt](const Request& req, const PathParams&) -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) {
                co_return make_error(http::status::forbidden, "missing or invalid token");
            }

            auto target = std::string_view(req.target());
            auto q      = std::string(query_param(target, "q"));
            if (q.size() < 2) {
                co_return make_error(http::status::bad_request,
                                     "q must be at least 2 characters");
            }

            uint32_t limit = 10;
            auto limit_sv = query_param(target, "limit");
            if (!limit_sv.empty()) {
                uint32_t parsed = 10;
                auto [p, ec] = std::from_chars(limit_sv.data(),
                                               limit_sv.data() + limit_sv.size(),
                                               parsed);
                if (ec == std::errc{}) {
                    limit = std::clamp(parsed, 1u, 20u);
                }
            }

            try {
                auto rows = co_await pg->execute(
                    [q, limit](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT id, username FROM users "
                            "WHERE username ILIKE $1 || '%' LIMIT $2",
                            q,
                            static_cast<int>(limit));
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
}

} // namespace Loomic
