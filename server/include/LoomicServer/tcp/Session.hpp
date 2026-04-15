#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

#include "LoomicServer/tcp/frame.hpp"

namespace Loomic {

class SessionRegistry;
class JwtService;
class RedisClient;
class CassandraClient;
class SnowflakeGen;

// Token-bucket rate limiter embedded per Session.
// Not thread-safe on its own — safe only because all Session code runs on the
// per-session strand, so no concurrent access is possible.
struct TokenBucket {
    double tokens      = 20.0;
    double max_tokens  = 20.0;
    double refill_rate = 5.0;   // tokens refilled per second
    std::chrono::steady_clock::time_point last_refill
        = std::chrono::steady_clock::now();

    // Returns true if the message is allowed; false if rate-limited.
    bool consume();
};

/// Represents one authenticated TCP session.
/// All member accesses after construction must occur on strand_.
class Session : public std::enable_shared_from_this<Session> {
public:
    using StrandType = boost::asio::strand<boost::asio::any_io_executor>;

    Session(SslStream                           socket,
            std::shared_ptr<SessionRegistry>    registry,
            std::shared_ptr<JwtService>         jwt,
            std::shared_ptr<RedisClient>        redis,
            std::shared_ptr<CassandraClient>    cass,
            std::shared_ptr<SnowflakeGen>       snowflake);

    /// Main coroutine: AUTH → offline flush → heartbeat → message loop → cleanup.
    /// Spawn with: net::co_spawn(session->strand(), session->run(), net::detached)
    boost::asio::awaitable<void> run();

    /// Returns the strand that all Session operations must run on.
    StrandType& strand() { return strand_; }

    /// Enqueue a pre-serialized frame for delivery.
    /// MUST be called from within strand_ (e.g. via net::post(strand_, ...)).
    void enqueue(std::vector<uint8_t> frame);

private:
    boost::asio::awaitable<void> flush_offline_queue();
    boost::asio::awaitable<void> heartbeat_loop();
    boost::asio::awaitable<void> route_message(const FrameHeader& hdr,
                                                std::vector<uint8_t> payload);
    boost::asio::awaitable<void> do_write();

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
};

} // namespace Loomic
