#include "LoomicServer/tcp/TcpServer.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

namespace Loomic {

TcpServer::TcpServer(net::io_context& ioc,
                     ssl::context& ssl_ctx,
                     uint16_t port)
    : ioc_(ioc), ssl_ctx_(ssl_ctx), port_(port)
{}

void TcpServer::start()
{
    LOG_INFO("TLS TCP server: port {}", port_);
    net::co_spawn(ioc_, listen(), net::detached);
}

net::awaitable<void> TcpServer::listen()
{
    auto executor = co_await net::this_coro::executor;
    net::ip::tcp::acceptor acceptor(
        executor,
        net::ip::tcp::endpoint(net::ip::tcp::v4(), port_));
    acceptor.set_option(net::socket_base::reuse_address(true));

    try {
        for (;;) {
            auto raw_socket = co_await acceptor.async_accept(net::use_awaitable);
            ssl::stream<net::ip::tcp::socket> tls_socket(std::move(raw_socket), ssl_ctx_);
            net::co_spawn(executor,
                          handle_connection(std::move(tls_socket)),
                          net::detached);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != net::error::operation_aborted) {
            LOG_WARN("TcpServer listener stopped: {}", e.what());
        }
    }
}

net::awaitable<void> TcpServer::handle_connection(
    ssl::stream<net::ip::tcp::socket> socket)
{
    try {
        co_await socket.async_handshake(ssl::stream_base::server, net::use_awaitable);
        // Phase 3: read wire frames here.
    } catch (...) {
        // Drop connection silently if TLS handshake fails.
    }
}

} // namespace Loomic
