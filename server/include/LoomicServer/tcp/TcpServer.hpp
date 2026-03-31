#pragma once

#include <cstdint>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace Loomic {

/// TLS TCP acceptor for the Loomic wire protocol (port 7777).
/// Performs a TLS handshake on every accepted connection; drops silently on failure.
/// Phase 3 will read wire frames inside handle_connection.
class TcpServer {
public:
    TcpServer(boost::asio::io_context&    ioc,
              boost::asio::ssl::context&  ssl_ctx,
              uint16_t                    port);

    /// Start the acceptor coroutine. Non-blocking; returns immediately.
    void start();

private:
    boost::asio::awaitable<void> listen();
    boost::asio::awaitable<void> handle_connection(
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket);

    boost::asio::io_context&   ioc_;
    boost::asio::ssl::context& ssl_ctx_;
    uint16_t                   port_;
};

} // namespace Loomic
