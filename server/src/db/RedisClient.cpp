#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <charconv>
#include <stdexcept>
#include <string>

#include <hiredis/hiredis.h>
#include <hiredis/hiredis_ssl.h>

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace Loomic {

RedisClient::RedisClient(const std::string& host, uint16_t port,
                         const std::string& password, bool use_ssl)
{
    if (use_ssl) {
        redisSSLContextError ssl_err = REDIS_SSL_CTX_NONE;
        redisSSLContext* ssl_ctx = redisCreateSSLContext(
            nullptr, nullptr, nullptr, nullptr, host.c_str(), &ssl_err);
        if (!ssl_ctx) {
            throw std::runtime_error(
                std::string("RedisClient: failed to create SSL context: ")
                + redisSSLContextGetError(ssl_err));
        }
        ctx_ = redisConnect(host.c_str(), port);
        if (!ctx_ || ctx_->err) {
            std::string err = ctx_ ? ctx_->errstr : "connection failed";
            redisFreeSSLContext(ssl_ctx);
            throw std::runtime_error("RedisClient: connect failed: " + err);
        }
        if (redisInitiateSSLWithContext(ctx_, ssl_ctx) != REDIS_OK) {
            std::string err = ctx_->errstr;
            redisFreeSSLContext(ssl_ctx);
            throw std::runtime_error("RedisClient: SSL handshake failed: " + err);
        }
        redisFreeSSLContext(ssl_ctx);
    } else {
        ctx_ = redisConnect(host.c_str(), port);
        if (!ctx_ || ctx_->err) {
            std::string err = ctx_ ? ctx_->errstr : "connection failed";
            throw std::runtime_error("RedisClient: connect failed: " + err);
        }
    }

    if (!password.empty()) {
        auto* reply = static_cast<redisReply*>(
            redisCommand(ctx_, "AUTH %s", password.c_str()));
        if (reply) freeReplyObject(reply);
    }

    LOG_INFO("RedisClient connected to {}:{}", host, port);
}

RedisClient::~RedisClient()
{
    if (ctx_) redisFree(ctx_);
    pool_.stop();
    pool_.join();
}

net::awaitable<void> RedisClient::lpush(const std::string& key,
                                         std::span<const uint8_t> data)
{
    std::vector<uint8_t> buf(data.begin(), data.end());
    std::string k = key;

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LPUSH %s %b", k.c_str(), buf.data(), buf.size()));
    if (reply) freeReplyObject(reply);

    // 7-day TTL on offline queue
    auto* exp_reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EXPIRE %s 604800", k.c_str()));
    if (exp_reply) freeReplyObject(exp_reply);
}

net::awaitable<std::vector<std::vector<uint8_t>>>
RedisClient::lrange_and_del(const std::string& key)
{
    std::string k = key;

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    std::vector<std::vector<uint8_t>> result;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "LRANGE %s 0 -1", k.c_str()));
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (size_t i = reply->elements; i > 0; --i) {
                auto* elem = reply->element[i - 1];
                if (elem && elem->type == REDIS_REPLY_STRING) {
                    result.emplace_back(
                        reinterpret_cast<const uint8_t*>(elem->str),
                        reinterpret_cast<const uint8_t*>(elem->str) + elem->len);
                }
            }
        }
        freeReplyObject(reply);
    }

    if (!result.empty()) {
        auto* del_reply = static_cast<redisReply*>(
            redisCommand(ctx_, "DEL %s", k.c_str()));
        if (del_reply) freeReplyObject(del_reply);
    }

    co_return result;
}

net::awaitable<void> RedisClient::set_presence(uint64_t user_id, std::string_view server_id)
{
    std::string key    = "presence:" + std::to_string(user_id);
    std::string val(server_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SETEX %s 60 %s", key.c_str(), val.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<void> RedisClient::refresh_presence(uint64_t user_id)
{
    std::string key = "presence:" + std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "EXPIRE %s 60", key.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<void> RedisClient::del_presence(uint64_t user_id)
{
    std::string key = "presence:" + std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "DEL %s", key.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<std::optional<std::string>> RedisClient::get_presence(uint64_t user_id)
{
    std::string key = "presence:" + std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    std::optional<std::string> result;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str()));
    if (reply) {
        if (reply->type == REDIS_REPLY_STRING) {
            result = std::string(reply->str, static_cast<std::size_t>(reply->len));
        }
        freeReplyObject(reply);
    }
    co_return result;
}

net::awaitable<void> RedisClient::sadd_group_member(uint64_t group_id, uint64_t user_id)
{
    std::string key = "group:" + std::to_string(group_id) + ":members";
    std::string val = std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SADD %s %s", key.c_str(), val.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<void> RedisClient::srem_group_member(uint64_t group_id, uint64_t user_id)
{
    std::string key = "group:" + std::to_string(group_id) + ":members";
    std::string val = std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SREM %s %s", key.c_str(), val.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<std::vector<uint64_t>> RedisClient::smembers_group(uint64_t group_id)
{
    std::string key = "group:" + std::to_string(group_id) + ":members";

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    std::vector<uint64_t> result;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SMEMBERS %s", key.c_str()));
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY) {
            for (std::size_t i = 0; i < reply->elements; ++i) {
                auto* elem = reply->element[i];
                if (elem && elem->type == REDIS_REPLY_STRING) {
                    uint64_t uid = 0;
                    auto [p, ec] = std::from_chars(elem->str, elem->str + elem->len, uid);
                    if (ec == std::errc{}) {
                        result.push_back(uid);
                    }
                }
            }
        }
        freeReplyObject(reply);
    }
    co_return result;
}

net::awaitable<void> RedisClient::hincrby_unread(uint64_t user_id, uint64_t conv_id, int64_t delta)
{
    std::string key   = "unread:" + std::to_string(user_id);
    std::string field = std::to_string(conv_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HINCRBY %s %s %lld", key.c_str(), field.c_str(), (long long)delta));
    if (reply) freeReplyObject(reply);
}

net::awaitable<std::unordered_map<uint64_t,int64_t>> RedisClient::hgetall_unread(uint64_t user_id)
{
    std::string key = "unread:" + std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    std::unordered_map<uint64_t,int64_t> result;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HGETALL %s", key.c_str()));
    if (reply) {
        if (reply->type == REDIS_REPLY_ARRAY && reply->elements % 2 == 0) {
            for (std::size_t i = 0; i + 1 < reply->elements; i += 2) {
                auto* fld = reply->element[i];
                auto* val = reply->element[i + 1];
                if (fld && fld->type == REDIS_REPLY_STRING &&
                    val && val->type == REDIS_REPLY_STRING) {
                    uint64_t conv_id = 0;
                    auto [p1, ec1] = std::from_chars(fld->str, fld->str + fld->len, conv_id);
                    int64_t count  = 0;
                    auto [p2, ec2] = std::from_chars(val->str, val->str + val->len, count);
                    if (ec1 == std::errc{} && ec2 == std::errc{}) {
                        result[conv_id] = count;
                    }
                }
            }
        }
        freeReplyObject(reply);
    }
    co_return result;
}

net::awaitable<void> RedisClient::hdel_unread(uint64_t user_id, uint64_t conv_id)
{
    std::string key   = "unread:" + std::to_string(user_id);
    std::string field = std::to_string(conv_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "HDEL %s %s", key.c_str(), field.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<void> RedisClient::set_last_seen(uint64_t user_id, int64_t timestamp_ms)
{
    std::string key = "last_seen:" + std::to_string(user_id);
    std::string val = std::to_string(timestamp_ms);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    // 7-day TTL
    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "SETEX %s 604800 %s", key.c_str(), val.c_str()));
    if (reply) freeReplyObject(reply);
}

net::awaitable<std::optional<int64_t>> RedisClient::get_last_seen(uint64_t user_id)
{
    std::string key = "last_seen:" + std::to_string(user_id);

    co_await net::post(pool_, net::use_awaitable);

    std::lock_guard lock(mutex_);
    std::optional<int64_t> result;

    auto* reply = static_cast<redisReply*>(
        redisCommand(ctx_, "GET %s", key.c_str()));
    if (reply) {
        if (reply->type == REDIS_REPLY_STRING) {
            int64_t ts = 0;
            auto [p, ec] = std::from_chars(reply->str, reply->str + reply->len, ts);
            if (ec == std::errc{}) {
                result = ts;
            }
        }
        freeReplyObject(reply);
    }
    co_return result;
}

} // namespace Loomic
