#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

#include "LoomicServer/tcp/ISession.hpp"
#include "LoomicServer/http/HttpServer.hpp"  // Request, PathParams typedefs

namespace Loomic {

class SessionRegistry;
class JwtService;
class RedisClient;
class CassandraClient;
class SnowflakeGen;
class PgPool;

/// WebSocket session.  Speaks JSON text frames to browser clients.
/// Receives binary (FrameHeader + proto) frames from other sessions via
/// enqueue(), strips the header, and re-serialises to JSON for delivery.
class WebSocketSession
    : public ISession,
      public std::enable_shared_from_this<WebSocketSession>
{
public:
    using WsStream    = boost::beast::websocket::stream<boost::beast::tcp_stream>;
    using StrandType  = boost::asio::strand<boost::asio::any_io_executor>;

    WebSocketSession(boost::asio::ip::tcp::socket     socket,
                     std::shared_ptr<SessionRegistry> registry,
                     std::shared_ptr<JwtService>      jwt,
                     std::shared_ptr<RedisClient>     redis,
                     std::shared_ptr<CassandraClient> cass,
                     std::shared_ptr<SnowflakeGen>    snowflake,
                     std::shared_ptr<PgPool>          pg,
                     std::string                      server_id);

    /// Accept the WebSocket upgrade and run the session until disconnect.
    boost::asio::awaitable<void> run(boost::beast::flat_buffer buffer,
                                     Request                   upgrade_req);

    // ISession
    void enqueue(std::vector<uint8_t> frame) override;
    StrandType& strand() override { return strand_; }

private:
    boost::asio::awaitable<void> read_loop();
    boost::asio::awaitable<void> do_write();
    boost::asio::awaitable<void> flush_offline_queue();
    boost::asio::awaitable<void> heartbeat_loop();
    void                         reset_heartbeat();
    boost::asio::awaitable<void> route_message(std::string_view json);
    boost::asio::awaitable<void> route_group_message(std::string_view json);
    boost::asio::awaitable<std::vector<uint64_t>> load_group_members_from_pg(uint64_t group_id);

    WsStream                         ws_;
    StrandType                       strand_;
    uint64_t                         user_id_{0};
    boost::asio::steady_timer        heartbeat_;
    std::deque<std::string>          write_queue_;
    bool                             writing_{false};

    std::shared_ptr<SessionRegistry> registry_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<RedisClient>     redis_;
    std::shared_ptr<CassandraClient> cass_;
    std::shared_ptr<SnowflakeGen>    snowflake_;
    std::shared_ptr<PgPool>          pg_;
    std::string                      server_id_;
};

} // namespace Loomic
