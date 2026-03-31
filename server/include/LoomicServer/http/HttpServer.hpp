#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>

namespace Loomic {

namespace http = boost::beast::http;

using Request  = http::request<http::string_body>;
using Response = http::response<http::string_body>;
using Handler  = std::function<boost::asio::awaitable<Response>(Request)>;

struct Route {
    http::verb  method;
    std::string path;
    Handler     handler;
};

/// Beast HTTP server with a simple method+path route table.
/// Replaces HealthHandler and adds /auth/* endpoints.
class HttpServer {
public:
    HttpServer(boost::asio::io_context& ioc, uint16_t port);

    void add_route(http::verb method, std::string path, Handler handler);

    /// Start the acceptor coroutine. Non-blocking; returns immediately.
    void start();

private:
    boost::asio::awaitable<void> listen();
    boost::asio::awaitable<void> handle_connection(boost::asio::ip::tcp::socket socket);

    boost::asio::io_context& ioc_;
    uint16_t                 port_;
    std::vector<Route>       routes_;
};

/// Build a JSON response with the given status and body string.
Response make_json(http::status status, std::string body);

/// Build a JSON error response: {"error":"<msg>"}.
Response make_error(http::status status, std::string_view msg);

} // namespace Loomic
