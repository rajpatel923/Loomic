#pragma once

#include <cstdint>
#include <memory>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace Loomic {

class SessionRegistry;
class JwtService;
class RedisClient;
class CassandraClient;
class SnowflakeGen;

/// TLS TCP acceptor for the Loomic wire protocol.
/// Performs a TLS handshake on every accepted connection, then hands the
/// authenticated socket off to a new Session coroutine.
class TcpServer {
public:
    TcpServer(boost::asio::io_context&          ioc,
              boost::asio::ssl::context&         ssl_ctx,
              uint16_t                           port,
              std::shared_ptr<SessionRegistry>   registry,
              std::shared_ptr<JwtService>        jwt,
              std::shared_ptr<RedisClient>       redis,
              std::shared_ptr<CassandraClient>   cass,
              std::shared_ptr<SnowflakeGen>      snowflake);

    /// Start the acceptor coroutine. Non-blocking; returns immediately.
    void start();

private:
    boost::asio::awaitable<void> listen();

    boost::asio::io_context&   ioc_;
    boost::asio::ssl::context& ssl_ctx_;
    uint16_t                   port_;
    std::shared_ptr<SessionRegistry>  registry_;
    std::shared_ptr<JwtService>       jwt_;
    std::shared_ptr<RedisClient>      redis_;
    std::shared_ptr<CassandraClient>  cass_;
    std::shared_ptr<SnowflakeGen>     snowflake_;
};

} // namespace Loomic
