#pragma once

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

struct redisContext;

namespace Loomic {

namespace net = boost::asio;

/// Thin async wrapper around synchronous hiredis calls.
/// Each public method posts work to a dedicated 2-thread pool and co_awaits
/// completion, freeing the calling strand for other work during the Redis RTT.
class RedisClient {
public:
    RedisClient(const std::string& host, uint16_t port,
                const std::string& password, bool use_ssl);
    ~RedisClient();

    /// Push raw bytes to the tail of a Redis list (LPUSH key data).
    net::awaitable<void> lpush(const std::string& key,
                                std::span<const uint8_t> data);

    /// LRANGE key 0 -1 then DEL key in one round-trip pair.
    /// Returns items oldest-first (LPUSH stores newest-first, so we reverse).
    net::awaitable<std::vector<std::vector<uint8_t>>>
    lrange_and_del(const std::string& key);

private:
    redisContext*    ctx_;
    net::thread_pool pool_{2};
    std::mutex       mutex_;
};

} // namespace Loomic
