#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/metrics/MetricsRegistry.hpp"
#include "LoomicServer/util/Logger.hpp"
#include "LoomicServer/util/RequestContext.hpp"
#include "LoomicServer/util/Uuid.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace beast = boost::beast;
namespace net   = boost::asio;

namespace Loomic {

Response make_json(http::status status, std::string body)
{
    Response res;
    res.result(status);
    res.set(http::field::content_type, "application/json");
    res.body() = std::move(body);
    return res;
}

Response make_error(http::status status, std::string_view msg)
{
    nlohmann::json j;
    j["error"] = msg;
    return make_json(status, j.dump());
}

// Split a string_view by a delimiter into a vector of string_views.
static std::vector<std::string_view> split_path(std::string_view s, char delim)
{
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (start < s.size()) {
        auto pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        if (pos > start) {
            parts.push_back(s.substr(start, pos - start));
        }
        start = pos + 1;
    }
    return parts;
}

bool HttpServer::match_path(std::string_view pattern, std::string_view target,
                             PathParams& params)
{
    // Strip query string from target before matching
    auto qpos = target.find('?');
    if (qpos != std::string_view::npos) {
        target = target.substr(0, qpos);
    }

    auto pat_parts = split_path(pattern, '/');
    auto tgt_parts = split_path(target,  '/');

    if (pat_parts.size() != tgt_parts.size()) return false;

    for (size_t i = 0; i < pat_parts.size(); ++i) {
        auto& p = pat_parts[i];
        auto& t = tgt_parts[i];
        if (p.size() >= 2 && p.front() == '{' && p.back() == '}') {
            // Path parameter: extract name and bind value
            std::string name(p.substr(1, p.size() - 2));
            params[name] = std::string(t);
        } else if (p != t) {
            return false;
        }
    }
    return true;
}

HttpServer::HttpServer(net::io_context& ioc, uint16_t port)
    : ioc_(ioc), port_(port)
{}

void HttpServer::add_route(http::verb method, std::string path, Handler handler)
{
    routes_.push_back({method, std::move(path), std::move(handler)});
}

void HttpServer::set_ws_upgrade_handler(WsUpgradeHandler h)
{
    ws_upgrade_handler_ = std::move(h);
}

void HttpServer::start()
{
    LOG_INFO("HTTP server: http://0.0.0.0:{}", port_);
    net::co_spawn(ioc_, listen(), net::detached);
}

net::awaitable<void> HttpServer::listen()
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::acceptor acceptor(
        executor,
        net::ip::tcp::endpoint(net::ip::tcp::v4(), port_));
    acceptor.set_option(net::socket_base::reuse_address(true));

    try {
        for (;;) {
            auto socket = co_await acceptor.async_accept(net::use_awaitable);
            net::co_spawn(executor,
                          handle_connection(std::move(socket)),
                          net::detached);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != net::error::operation_aborted) {
            LOG_WARN("HttpServer listener stopped: {}", e.what());
        }
    }
}

net::awaitable<void> HttpServer::handle_connection(net::ip::tcp::socket socket)
{
    try {
        // ── Per-request tracing & timing ─────────────────────────────────────
        g_request_ctx.request_id = generate_uuid_v4();
        auto t0 = std::chrono::steady_clock::now();

        beast::flat_buffer buffer;
        Request req;
        co_await http::async_read(socket, buffer, req, net::use_awaitable);

        // ── WebSocket upgrade ────────────────────────────────────────────────
        if (ws_upgrade_handler_ && beast::websocket::is_upgrade(req)
            && req.target() == "/ws")
        {
            co_await ws_upgrade_handler_(
                std::move(socket), std::move(buffer), std::move(req));
            g_request_ctx.request_id.clear();
            co_return;
        }

        // ── CORS preflight ───────────────────────────────────────────────────
        if (req.method() == http::verb::options) {
            Response res{http::status::no_content, req.version()};
            res.set("Access-Control-Allow-Origin", "*");
            res.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
            res.set("Access-Control-Max-Age", "86400");
            res.set("X-Request-ID", g_request_ctx.request_id);
            res.keep_alive(false);
            res.prepare_payload();
            co_await http::async_write(socket, res, net::use_awaitable);
            g_request_ctx.request_id.clear();
            co_return;
        }

        // ── Route matching ───────────────────────────────────────────────────
        Response res;
        bool handled = false;
        std::string target(req.target());
        for (const auto& route : routes_) {
            if (route.method != req.method()) continue;
            PathParams params;
            if (match_path(route.path, target, params)) {
                res = co_await route.handler(req, params);
                handled = true;
                break;
            }
        }
        if (!handled) {
            res = make_error(http::status::not_found, "not found");
        }

        // ── Record HTTP metrics ───────────────────────────────────────────────
        {
            auto elapsed_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - t0).count();
            std::string method_str(req.method_string());
            std::string status_str = std::to_string(
                static_cast<unsigned>(res.result_int()));
            try {
                auto& mr = MetricsRegistry::get();
                mr.http_requests_total(method_str, status_str).Increment();
                mr.http_latency_ms().Observe(elapsed_ms);
            } catch (...) {}
        }

        // ── Inject CORS + tracing headers on every response ──────────────────
        res.set("Access-Control-Allow-Origin", "*");
        res.set("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set("Access-Control-Allow-Headers", "Content-Type, Authorization");
        res.set("X-Request-ID", g_request_ctx.request_id);

        res.version(req.version());
        res.keep_alive(false);
        res.prepare_payload();

        co_await http::async_write(socket, res, net::use_awaitable);

        beast::error_code ec;
        socket.shutdown(net::ip::tcp::socket::shutdown_send, ec);
    } catch (...) {
    }

    g_request_ctx.request_id.clear();
}

} // namespace Loomic
