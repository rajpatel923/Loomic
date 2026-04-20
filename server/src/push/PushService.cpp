#include "LoomicServer/push/PushService.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/use_awaitable.hpp>

#include <pqxx/pqxx>

#include <string>

namespace net = boost::asio;

namespace Loomic {

PushService::PushService(std::shared_ptr<PgPool> pg)
    : pg_(std::move(pg))
{}

net::awaitable<void> PushService::register_token(uint64_t user_id,
                                                   const std::string& token,
                                                   const std::string& platform)
{
    try {
        co_await pg_->execute(
            [user_id, token, platform](pqxx::connection& conn) {
                pqxx::work txn(conn);
                txn.exec_params(
                    "INSERT INTO device_tokens (user_id, token, platform, updated_at) "
                    "VALUES ($1, $2, $3, NOW()) "
                    "ON CONFLICT (user_id, token) DO UPDATE "
                    "SET platform=$3, updated_at=NOW()",
                    static_cast<int64_t>(user_id), token, platform);
                txn.commit();
                return pqxx::result{};
            },
            PgPool::RetryClass::NonRetryableWrite);
    } catch (const std::exception& e) {
        LOG_ERROR("PushService::register_token user_id={}: {}", user_id, e.what());
    }
}

net::awaitable<void> PushService::unregister_token(uint64_t user_id,
                                                     const std::string& token)
{
    try {
        co_await pg_->execute(
            [user_id, token](pqxx::connection& conn) {
                pqxx::work txn(conn);
                txn.exec_params(
                    "DELETE FROM device_tokens WHERE user_id=$1 AND token=$2",
                    static_cast<int64_t>(user_id), token);
                txn.commit();
                return pqxx::result{};
            },
            PgPool::RetryClass::NonRetryableWrite);
    } catch (const std::exception& e) {
        LOG_ERROR("PushService::unregister_token user_id={}: {}", user_id, e.what());
    }
}

net::awaitable<void> PushService::send_push(uint64_t user_id,
                                             std::string_view title,
                                             std::string_view body)
{
    LOG_INFO("PUSH(stub) user={} title={} body={}", user_id, title, body);
    co_return;
}

} // namespace Loomic
