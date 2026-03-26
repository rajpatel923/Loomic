#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>

#include "LoomicServer/http/HealthHandler.hpp"

namespace Loomic {

struct Config;

class Server {
public:
    explicit Server(const Config& cfg);

    /// Block until a shutdown signal (SIGINT / SIGTERM) is received.
    void run();

    /// Gracefully stop all io_contexts. Safe to call from any thread.
    void shutdown();

private:
    unsigned int                            thread_count_;
    std::vector<boost::asio::io_context>    io_ctxs_;
    boost::asio::signal_set                 signals_;
    HealthHandler                           health_;
    std::vector<std::thread>                threads_;
    std::atomic<int>                        rr_index_{0};
};

} // namespace Loomic
