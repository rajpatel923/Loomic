#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include "LoomicServer/tcp/ISession.hpp"
#include "LoomicServer/tcp/frame.hpp"

namespace Loomic {

class SessionRegistry;
class JwtService;
class RedisClient;
class CassandraClient;
class SnowflakeGen;
class PgPool;

struct TokenBucket {
    double tokens      = 20.0;
    double max_tokens  = 20.0;
    double refill_rate = 5.0;
    std::chrono::steady_clock::time_point last_refill
        = std::chrono::steady_clock::now();

    bool consume();
};

class Session : public ISession, public std::enable_shared_from_this<Session> {
public:
    using StrandType = boost::asio::strand<boost::asio::any_io_executor>;

    Session(SslStream                           socket,
            std::shared_ptr<SessionRegistry>    registry,
            std::shared_ptr<JwtService>         jwt,
            std::shared_ptr<RedisClient>        redis,
            std::shared_ptr<CassandraClient>    cass,
            std::shared_ptr<SnowflakeGen>       snowflake,
            std::shared_ptr<PgPool>             pg,
            std::string                         server_id);

    boost::asio::awaitable<void> run();
    StrandType& strand() override { return strand_; }
    void enqueue(std::vector<uint8_t> frame) override;

private:
    boost::asio::awaitable<void> flush_offline_queue();
    boost::asio::awaitable<void> heartbeat_loop();
    boost::asio::awaitable<void> route_message(const FrameHeader& hdr,
                                                std::vector<uint8_t> payload);
    boost::asio::awaitable<void> do_write();
    boost::asio::awaitable<void> upsert_conv_members(uint64_t conv_id,
                                                      uint64_t user_a,
                                                      uint64_t user_b);
    boost::asio::awaitable<std::vector<uint64_t>> load_group_members_from_pg(uint64_t group_id);
    void reset_heartbeat();

    SslStream                        socket_;
    StrandType                       strand_;
    uint64_t                         user_id_{0};
    boost::asio::steady_timer        heartbeat_;
    std::deque<std::vector<uint8_t>> write_queue_;
    bool                             writing_{false};
    TokenBucket                      rate_limiter_;

    std::shared_ptr<SessionRegistry> registry_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<RedisClient>     redis_;
    std::shared_ptr<CassandraClient> cass_;
    std::shared_ptr<SnowflakeGen>    snowflake_;
    std::shared_ptr<PgPool>          pg_;
    std::string                      server_id_;
};

} // namespace Loomic
