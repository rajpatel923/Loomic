#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>
#include <pqxx/pqxx>

namespace Loomic {

/// Blocking libpqxx connection pool with async execution via a thread_pool.
/// Pool size == thread_count so acquire() never blocks in practice.
class PgPool {
public:
    using DbFunc = std::function<pqxx::result(pqxx::connection&)>;

    PgPool(const std::string& conn_str,
           unsigned int pool_size,
           boost::asio::thread_pool& tp);
    ~PgPool();

    /// Hops to thread_pool, acquires a connection, runs func, releases it.
    boost::asio::awaitable<pqxx::result> execute(DbFunc func);

private:
    pqxx::connection* acquire();
    void release(pqxx::connection* conn);

    std::vector<std::unique_ptr<pqxx::connection>> connections_;
    std::vector<pqxx::connection*>                 free_list_;
    std::mutex                                     mutex_;
    std::condition_variable                        cv_;
    boost::asio::thread_pool&                      pool_;
};

} // namespace Loomic
