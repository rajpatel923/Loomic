#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <pqxx/pqxx>

namespace Loomic {

/// Blocking libpqxx connection pool with async execution via a thread_pool.
/// Pool size == thread_count so acquire() never blocks in practice.
class PgPool {
public:
    enum class RetryClass {
        ReadOnly,
        RetrySafeWrite,
        NonRetryableWrite,
    };

    using DbFunc = std::function<pqxx::result(pqxx::connection&)>;

    PgPool(const std::string& conn_str,
           unsigned int pool_size,
           boost::asio::thread_pool& tp);
    ~PgPool();

    /// Hops to thread_pool, acquires a connection, runs func, releases it.
    boost::asio::awaitable<pqxx::result> execute(
        DbFunc func,
        RetryClass retry_class = RetryClass::NonRetryableWrite);

private:
    struct Slot {
        std::unique_ptr<pqxx::connection>           conn;
        std::chrono::steady_clock::time_point       created_at{};
        std::chrono::steady_clock::time_point       last_used_at{};
        std::chrono::steady_clock::time_point       last_validated_at{};
    };

    std::size_t acquire();
    void release(std::size_t index);
    void ensure_connection(Slot& slot);
    void recreate_connection(Slot& slot, std::string_view reason);
    void probe_connection(Slot& slot);
    static bool should_retry_once(RetryClass retry_class) noexcept;
    static bool should_probe_idle_connection(
        const Slot& slot,
        std::chrono::steady_clock::time_point now) noexcept;
    static bool should_recycle_connection(
        const Slot& slot,
        std::chrono::steady_clock::time_point now) noexcept;

    std::string                                    conn_str_;
    std::vector<Slot>                              connections_;
    std::vector<std::size_t>                       free_list_;
    std::mutex                                     mutex_;
    std::condition_variable                        cv_;
    boost::asio::thread_pool&                      pool_;
};

} // namespace Loomic
