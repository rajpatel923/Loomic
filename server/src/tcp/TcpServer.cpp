#include "LoomicServer/tcp/TcpServer.hpp"
#include "LoomicServer/tcp/Session.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

namespace Loomic {

TcpServer::TcpServer(net::io_context& ioc,
                     ssl::context& ssl_ctx,
                     uint16_t port,
                     std::shared_ptr<SessionRegistry>  registry,
                     std::shared_ptr<JwtService>       jwt,
                     std::shared_ptr<RedisClient>      redis,
                     std::shared_ptr<CassandraClient>  cass,
                     std::shared_ptr<SnowflakeGen>     snowflake,
                     std::shared_ptr<PgPool>           pg,
                     std::string                       server_id)
    : ioc_(ioc)
    , ssl_ctx_(ssl_ctx)
    , port_(port)
    , registry_(std::move(registry))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
    , cass_(std::move(cass))
    , snowflake_(std::move(snowflake))
    , pg_(std::move(pg))
    , server_id_(std::move(server_id))
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
            auto raw = co_await acceptor.async_accept(net::use_awaitable);

            // Spawn a per-connection coroutine that does the TLS handshake
            // and then hands the socket to a Session.
            net::co_spawn(executor,
                [this, raw = std::move(raw)]() mutable -> net::awaitable<void>
                {
                    ssl::stream<net::ip::tcp::socket> tls(std::move(raw), ssl_ctx_);
                    try {
                        co_await tls.async_handshake(ssl::stream_base::server,
                                                      net::use_awaitable);
                    } catch (...) {
                        co_return; // Drop connection silently on handshake failure.
                    }
                    auto session = std::make_shared<Session>(
                        std::move(tls),
                        registry_, jwt_, redis_, cass_, snowflake_,
                        pg_, server_id_);
                    net::co_spawn(session->strand(), session->run(), net::detached);
                },
                net::detached);
        }
    } catch (const boost::system::system_error& e) {
        if (e.code() != net::error::operation_aborted) {
            LOG_WARN("TcpServer listener stopped: {}", e.what());
        }
    }
}

} // namespace Loomic
