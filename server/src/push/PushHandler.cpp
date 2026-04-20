#include "LoomicServer/push/PushHandler.hpp"
#include "LoomicServer/push/PushService.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>

#include <nlohmann/json.hpp>

#include <string>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

PushHandler::PushHandler(std::shared_ptr<PgPool>      pg,
                          std::shared_ptr<JwtService>  jwt,
                          std::shared_ptr<PushService> push_service)
    : pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , push_service_(std::move(push_service))
{}

void PushHandler::register_routes(HttpServer& http_server)
{
    // POST /push/register
    http_server.add_route(http::verb::post, "/push/register",
        [this](const Request& req, const PathParams&) -> net::awaitable<Response>
        {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
            if (!user) {
                co_return make_error(http::status::forbidden, "missing or invalid token");
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body());
            } catch (...) {
                co_return make_error(http::status::bad_request, "invalid JSON");
            }

            auto token    = body.value("token", std::string{});
            auto platform = body.value("platform", std::string{});
            if (token.empty() || platform.empty()) {
                co_return make_error(http::status::bad_request, "token and platform required");
            }

            auto user_id = static_cast<uint64_t>(user->uid);
            co_await push_service_->register_token(user_id, token, platform);
            co_return make_json(http::status::ok, R"({"ok":true})");
        });

    // DELETE /push/register
    http_server.add_route(http::verb::delete_, "/push/register",
        [this](const Request& req, const PathParams&) -> net::awaitable<Response>
        {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
            if (!user) {
                co_return make_error(http::status::forbidden, "missing or invalid token");
            }

            nlohmann::json body;
            try {
                body = nlohmann::json::parse(req.body());
            } catch (...) {
                co_return make_error(http::status::bad_request, "invalid JSON");
            }

            auto token = body.value("token", std::string{});
            if (token.empty()) {
                co_return make_error(http::status::bad_request, "token required");
            }

            auto user_id = static_cast<uint64_t>(user->uid);
            co_await push_service_->unregister_token(user_id, token);
            co_return make_json(http::status::no_content, "");
        });
}

} // namespace Loomic
