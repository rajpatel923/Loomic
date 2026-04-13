#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace net = boost::asio;

namespace Loomic {

namespace {

constexpr auto kConnectionValidationIdleThreshold = std::chrono::minutes(5);
constexpr auto kConnectionMaxLifetime = std::chrono::minutes(30);

} // namespace

PgPool::PgPool(const std::string& conn_str,
               unsigned int pool_size,
               net::thread_pool& tp)
    : conn_str_(conn_str)
    , pool_(tp)
{
    connections_.reserve(pool_size);
    free_list_.reserve(pool_size);
    auto now = std::chrono::steady_clock::now();
    for (unsigned int i = 0; i < pool_size; ++i) {
        auto& slot = connections_.emplace_back();
        slot.conn = std::make_unique<pqxx::connection>(conn_str_);
        slot.created_at = now;
        slot.last_used_at = now;
        slot.last_validated_at = now;
        free_list_.push_back(i);
    }
    LOG_INFO("PgPool: {} connections established", pool_size);
}

PgPool::~PgPool() = default;

std::size_t PgPool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !free_list_.empty(); });
    auto index = free_list_.back();
    free_list_.pop_back();
    return index;
}

void PgPool::release(std::size_t index)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.push_back(index);
    }
    cv_.notify_one();
}

bool PgPool::should_retry_once(RetryClass retry_class) noexcept
{
    return retry_class != RetryClass::NonRetryableWrite;
}

bool PgPool::should_probe_idle_connection(
    const Slot& slot,
    std::chrono::steady_clock::time_point now) noexcept
{
    return now - slot.last_used_at >= kConnectionValidationIdleThreshold &&
           now - slot.last_validated_at >= kConnectionValidationIdleThreshold;
}

bool PgPool::should_recycle_connection(
    const Slot& slot,
    std::chrono::steady_clock::time_point now) noexcept
{
    return now - slot.created_at >= kConnectionMaxLifetime;
}

void PgPool::recreate_connection(Slot& slot, std::string_view reason)
{
    LOG_WARN("PgPool: recreating PostgreSQL connection ({})", reason);
    slot.conn = std::make_unique<pqxx::connection>(conn_str_);
    auto now = std::chrono::steady_clock::now();
    slot.created_at = now;
    slot.last_used_at = now;
    slot.last_validated_at = now;
}

void PgPool::probe_connection(Slot& slot)
{
    pqxx::nontransaction ntxn(*slot.conn);
    ntxn.exec("SELECT 1");
    slot.last_validated_at = std::chrono::steady_clock::now();
}

void PgPool::ensure_connection(Slot& slot)
{
    auto now = std::chrono::steady_clock::now();
    if (!slot.conn || !slot.conn->is_open()) {
        recreate_connection(slot, "checkout_not_open");
        return;
    }

    if (should_recycle_connection(slot, now)) {
        recreate_connection(slot, "max_lifetime_recycle");
        return;
    }

    if (should_probe_idle_connection(slot, now)) {
        try {
            probe_connection(slot);
        } catch (const pqxx::broken_connection&) {
            recreate_connection(slot, "idle_validation_failed");
        }
    }
}

net::awaitable<pqxx::result> PgPool::execute(DbFunc func, RetryClass retry_class)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);
    auto index = acquire();
    auto& slot = connections_[index];

    try {
        ensure_connection(slot);
        auto result = func(*slot.conn);
        slot.last_used_at = std::chrono::steady_clock::now();
        release(index);
        co_return result;
    } catch (const pqxx::broken_connection& e) {
        LOG_WARN("PgPool: PostgreSQL connection dropped: {}", e.what());

        if (!should_retry_once(retry_class)) {
            release(index);
            throw;
        }

        try {
            recreate_connection(slot, "broken_connection");
            auto result = func(*slot.conn);
            slot.last_used_at = std::chrono::steady_clock::now();
            release(index);
            co_return result;
        } catch (...) {
            release(index);
            throw;
        }
    } catch (...) {
        release(index);
        throw;
    }
}

} // namespace Loomic
