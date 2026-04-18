#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/http.hpp>

namespace Loomic {

namespace http = boost::beast::http;

using Request    = http::request<http::string_body>;
using Response   = http::response<http::string_body>;
using PathParams = std::unordered_map<std::string, std::string>;
using Handler    = std::function<
    boost::asio::awaitable<Response>(const Request&, const PathParams&)>;

struct Route {
    http::verb  method;
    std::string path;
    Handler     handler;
};

/// Beast HTTP server with method+path route table supporting {param} path segments.
class HttpServer {
public:
    /// Invoked when a WebSocket upgrade request arrives for /ws.
    using WsUpgradeHandler = std::function<
        boost::asio::awaitable<void>(boost::asio::ip::tcp::socket,
                                     boost::beast::flat_buffer, Request)>;

    HttpServer(boost::asio::io_context& ioc, uint16_t port);

    void add_route(http::verb method, std::string path, Handler handler);

    /// Register a handler that takes over the connection for WebSocket upgrades.
    void set_ws_upgrade_handler(WsUpgradeHandler h);

    void start();

private:
    boost::asio::awaitable<void> listen();
    boost::asio::awaitable<void> handle_connection(boost::asio::ip::tcp::socket socket);

    /// Returns true if pattern matches target, extracting {name} segments into params.
    static bool match_path(std::string_view pattern, std::string_view target,
                           PathParams& params);

    boost::asio::io_context& ioc_;
    uint16_t                 port_;
    std::vector<Route>       routes_;
    WsUpgradeHandler         ws_upgrade_handler_;
};

/// Build a JSON response with the given status and body string.
Response make_json(http::status status, std::string body);

/// Build a JSON error response: {"error":"<msg>"}.
Response make_error(http::status status, std::string_view msg);

} // namespace Loomic
