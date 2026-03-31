#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace net = boost::asio;

namespace Loomic {

PgPool::PgPool(const std::string& conn_str,
               unsigned int pool_size,
               net::thread_pool& tp)
    : pool_(tp)
{
    connections_.reserve(pool_size);
    free_list_.reserve(pool_size);
    for (unsigned int i = 0; i < pool_size; ++i) {
        connections_.emplace_back(std::make_unique<pqxx::connection>(conn_str));
        free_list_.push_back(connections_.back().get());
    }
    LOG_INFO("PgPool: {} connections established", pool_size);
}

PgPool::~PgPool() = default;

pqxx::connection* PgPool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !free_list_.empty(); });
    auto* conn = free_list_.back();
    free_list_.pop_back();
    return conn;
}

void PgPool::release(pqxx::connection* conn)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        free_list_.push_back(conn);
    }
    cv_.notify_one();
}

net::awaitable<pqxx::result> PgPool::execute(DbFunc func)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);
    auto* conn = acquire();
    try {
        auto result = func(*conn);
        release(conn);
        co_return result;
    } catch (...) {
        release(conn);
        throw;
    }
}

} // namespace Loomic
