#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

#include <nlohmann/json.hpp>

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

HttpServer::HttpServer(net::io_context& ioc, uint16_t port)
    : ioc_(ioc), port_(port)
{}

void HttpServer::add_route(http::verb method, std::string path, Handler handler)
{
    routes_.push_back({method, std::move(path), std::move(handler)});
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
        beast::flat_buffer buffer;
        Request req;
        co_await http::async_read(socket, buffer, req, net::use_awaitable);

        Response res;
        bool handled = false;
        for (const auto& route : routes_) {
            if (route.method == req.method() && route.path == req.target()) {
                res = co_await route.handler(req);
                handled = true;
                break;
            }
        }
        if (!handled) {
            res = make_error(http::status::not_found, "not found");
        }

        res.version(req.version());
        res.keep_alive(false);
        res.prepare_payload();

        co_await http::async_write(socket, res, net::use_awaitable);

        beast::error_code ec;
        socket.shutdown(net::ip::tcp::socket::shutdown_send, ec);
    } catch (...) {
        // Connection errors and client disconnects are expected.
    }
}

} // namespace Loomic
