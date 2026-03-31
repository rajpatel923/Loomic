#pragma once

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/thread_pool.hpp>

#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/tcp/TcpServer.hpp"

namespace Loomic {

struct Config;
class PgPool;
class SnowflakeGen;
class JwtService;
class PasswordService;

class Server {
public:
    explicit Server(const Config& cfg);

    /// Block until a shutdown signal (SIGINT / SIGTERM) is received.
    void run();

    /// Gracefully stop all io_contexts. Safe to call from any thread.
    void shutdown();

private:
    void setup_tls(const Config& cfg);
    void register_routes();

    // Declaration order matters: ssl_ctx_ must be constructed before tcp_.
    unsigned int                            thread_count_;
    boost::asio::ssl::context               ssl_ctx_;
    boost::asio::thread_pool                thread_pool_;
    std::vector<boost::asio::io_context>    io_ctxs_;
    boost::asio::signal_set                 signals_;
    std::shared_ptr<PgPool>                 pg_;
    std::shared_ptr<SnowflakeGen>           snowflake_;
    std::shared_ptr<JwtService>             jwt_;
    std::shared_ptr<PasswordService>        pwd_;
    HttpServer                              http_;
    TcpServer                               tcp_;
    std::vector<std::thread>                threads_;
    std::atomic<int>                        rr_index_{0};
};

} // namespace Loomic
