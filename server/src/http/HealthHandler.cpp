#include "LoomicServer/http/HealthHandler.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;

namespace Loomic {

HealthHandler::HealthHandler(net::io_context& ioc, uint16_t port)
    : ioc_(ioc), port_(port)
{
}

void HealthHandler::start()
{
    LOG_INFO("Health endpoint: http://0.0.0.0:{}/health", port_);
    net::co_spawn(ioc_, listen(), net::detached);
}

net::awaitable<void> HealthHandler::listen()
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
            LOG_WARN("HealthHandler listener stopped: {}", e.what());
        }
    }
}

net::awaitable<void> HealthHandler::handle_connection(net::ip::tcp::socket socket)
{
    try {
        beast::flat_buffer buffer;

        http::request<http::string_body> req;
        co_await http::async_read(socket, buffer, req, net::use_awaitable);

        http::response<http::string_body> res;
        res.version(req.version());
        res.keep_alive(false);

        if (req.method() == http::verb::get && req.target() == "/health") {
            res.result(http::status::ok);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"status":"ok", "health": "running"})";
        } else {
            res.result(http::status::not_found);
            res.set(http::field::content_type, "application/json");
            res.body() = R"({"error":"not found"})";
        }

        res.prepare_payload();

        co_await http::async_write(socket, res, net::use_awaitable);

        beast::error_code sec;
        socket.shutdown(net::ip::tcp::socket::shutdown_send, sec);

    } catch (...) {
        // Connection errors (partial reads, client disconnect) are expected.
    }
}

} // namespace Loomic
