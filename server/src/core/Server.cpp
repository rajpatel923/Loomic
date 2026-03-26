#include "LoomicServer/core/Server.hpp"
#include "LoomicServer/util/Config.hpp"
#include "LoomicServer/util/Logger.hpp"
#include <thread>

namespace net = boost::asio;

namespace Loomic {

Server::Server(const Config& cfg)
    : thread_count_(cfg.thread_count == 0
                        ? std::max(1u, std::thread::hardware_concurrency())
                        : cfg.thread_count)
    , io_ctxs_(thread_count_)
    , signals_(io_ctxs_[0], SIGINT, SIGTERM)
    , health_(io_ctxs_[0], cfg.http_health_port)
{
}

void Server::run()
{
    LOG_INFO("Loomic Server starting");
    LOG_INFO("Thread pool: {} threads", thread_count_);

    // Start health check listener (logs its own port)
    health_.start();

    // Register signal handler
    signals_.async_wait([this](const boost::system::error_code& ec, int /*signo*/) {
        if (!ec) {
            LOG_INFO("Shutdown signal received, stopping...");
            shutdown();
        }
    });

    // Spawn worker threads for io_ctxs_[1..N-1]
    threads_.reserve(thread_count_ - 1);
    for (unsigned i = 1; i < thread_count_; ++i) {
        threads_.emplace_back([this, i] {
            io_ctxs_[i].run();
        });
    }

    // Main thread drives io_ctxs_[0] (signals + health handler)
    io_ctxs_[0].run();

    // Join all worker threads
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }

    LOG_INFO("Server stopped");
}

void Server::shutdown()
{
    for (auto& ctx : io_ctxs_) {
        ctx.stop();
    }
}

} // namespace Loomic
