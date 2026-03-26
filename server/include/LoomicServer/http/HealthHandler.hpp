#pragma once

#include <cstdint>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace Loomic {

/// Beast HTTP listener that serves GET /health → {"status":"ok"}.
/// One instance is created by Server and lives for the process lifetime.
class HealthHandler {
public:
    HealthHandler(boost::asio::io_context& ioc, uint16_t port);

    /// Start the acceptor coroutine. Non-blocking; returns immediately.
    void start();

private:
    boost::asio::awaitable<void> listen();
    boost::asio::awaitable<void> handle_connection(boost::asio::ip::tcp::socket socket);

    boost::asio::io_context& ioc_;
    uint16_t                 port_;
};

} // namespace Loomic
