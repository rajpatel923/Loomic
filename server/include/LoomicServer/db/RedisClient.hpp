#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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

    /// GET presence:{user_id} — returns server_id string if online, nullopt if absent.
    net::awaitable<std::optional<std::string>> get_presence(uint64_t user_id);

    /// SADD group:{group_id}:members {user_id}
    net::awaitable<void> sadd_group_member(uint64_t group_id, uint64_t user_id);

    /// SREM group:{group_id}:members {user_id}
    net::awaitable<void> srem_group_member(uint64_t group_id, uint64_t user_id);

    /// SMEMBERS group:{group_id}:members — returns member user IDs (empty on miss).
    net::awaitable<std::vector<uint64_t>> smembers_group(uint64_t group_id);

    // ── Unread counters ───────────────────────────────────────────────────────
    /// HINCRBY unread:{user_id} {conv_id} delta
    net::awaitable<void> hincrby_unread(uint64_t user_id, uint64_t conv_id, int64_t delta = 1);
    /// HGETALL unread:{user_id} → map of conv_id → unread count
    net::awaitable<std::unordered_map<uint64_t,int64_t>> hgetall_unread(uint64_t user_id);
    /// HDEL unread:{user_id} {conv_id}
    net::awaitable<void> hdel_unread(uint64_t user_id, uint64_t conv_id);

    // ── Last-seen ─────────────────────────────────────────────────────────────
    /// SETEX last_seen:{user_id} 604800 {timestamp_ms}
    net::awaitable<void>                  set_last_seen(uint64_t user_id, int64_t timestamp_ms);
    /// GET last_seen:{user_id} → ms epoch, or nullopt
    net::awaitable<std::optional<int64_t>> get_last_seen(uint64_t user_id);

private:
    redisContext*    ctx_;
    net::thread_pool pool_{2};
    std::mutex       mutex_;
};

} // namespace Loomic
