#pragma once

#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

struct redisContext;

namespace Loomic {

namespace net = boost::asio;

class RedisClient {
public:
    RedisClient(const std::string& host, uint16_t port,
                const std::string& password, bool use_ssl);
    ~RedisClient();

    /// Push raw bytes to the head of a Redis list (LPUSH key data) + EXPIRE 7 days.
    net::awaitable<void> lpush(const std::string& key,
                                std::span<const uint8_t> data);

    /// LRANGE key 0 -1 then DEL key. Returns items oldest-first.
    net::awaitable<std::vector<std::vector<uint8_t>>>
    lrange_and_del(const std::string& key);

    /// SETEX presence:{user_id} 60 {server_id}
    net::awaitable<void> set_presence(uint64_t user_id, std::string_view server_id);

    /// EXPIRE presence:{user_id} 60
    net::awaitable<void> refresh_presence(uint64_t user_id);

    /// DEL presence:{user_id}
    net::awaitable<void> del_presence(uint64_t user_id);

private:
    redisContext*    ctx_;
    net::thread_pool pool_{2};
    std::mutex       mutex_;
};

} // namespace Loomic
