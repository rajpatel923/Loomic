#include "LoomicServer/core/Server.hpp"
#include "LoomicServer/metrics/MetricsRegistry.hpp"
#include "LoomicServer/util/Config.hpp"
#include "LoomicServer/util/Logger.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/PasswordService.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/http/DocsHandler.hpp"
#include "LoomicServer/http/ConversationsHandler.hpp"
#include "LoomicServer/http/GroupsHandler.hpp"
#include "LoomicServer/http/MessagesHandler.hpp"
#include "LoomicServer/http/UsersHandler.hpp"
#include "LoomicServer/http/ReceiptsHandler.hpp"
#include "LoomicServer/push/PushService.hpp"
#include "LoomicServer/push/PushHandler.hpp"
#include "LoomicServer/ws/WebSocketSession.hpp"
#include <openssl/ssl.h>

#include <boost/asio/ip/host_name.hpp>

#include <fstream>
#include <sstream>
#include <thread>

namespace net = boost::asio;
namespace ssl = boost::asio::ssl;

namespace Loomic {

Server::Server(const Config& cfg)
    : thread_count_(cfg.thread_count == 0
                        ? std::max(1u, std::thread::hardware_concurrency())
                        : cfg.thread_count)
    , ssl_ctx_(ssl::context::tls_server)
    , thread_pool_(thread_count_)
    , io_ctxs_(thread_count_)
    , signals_(io_ctxs_[0], SIGINT, SIGTERM)
    , pg_(std::make_shared<PgPool>(cfg.pg_conn_string, thread_count_, thread_pool_))
    , snowflake_(std::make_shared<SnowflakeGen>())
    , jwt_(std::make_shared<JwtService>(cfg.jwt_secret))
    , pwd_(std::make_shared<PasswordService>(thread_pool_))
    , redis_(std::make_shared<RedisClient>(cfg.redis_host, cfg.redis_port,
                                           cfg.redis_password, cfg.redis_ssl))
    , cass_(std::make_shared<CassandraClient>(cfg))
    , registry_(std::make_shared<SessionRegistry>())
    , http_(io_ctxs_[0], cfg.http_health_port)
    , tcp_(io_ctxs_[0], ssl_ctx_, cfg.port,
           registry_, jwt_, redis_, cass_, snowflake_,
           pg_,
           net::ip::host_name() + ":" + std::to_string(cfg.port))
{
    MetricsRegistry::init(cfg.metrics_port);
    setup_tls(cfg);
    register_routes(cfg);
}

void Server::setup_tls(const Config& cfg)
{
    ssl_ctx_.set_options(ssl::context::default_workarounds
        | ssl::context::no_sslv2
        | ssl::context::no_sslv3
        | ssl::context::no_tlsv1
        | ssl::context::no_tlsv1_1);
    SSL_CTX_set_min_proto_version(ssl_ctx_.native_handle(), TLS1_2_VERSION);

    if (!cfg.tls_cert_path.empty()) {
        ssl_ctx_.use_certificate_chain_file(cfg.tls_cert_path);
        ssl_ctx_.use_private_key_file(cfg.tls_key_path, ssl::context::pem);
    }
}

void Server::register_routes(const Config& cfg)
{
    http_.add_route(http::verb::get, "/health",
        [](const Request& /*req*/, const PathParams&) -> net::awaitable<Response> {
            co_return make_json(http::status::ok, R"({"status":"ok"})");
        });

    register_auth_routes(http_, pg_, snowflake_, jwt_, pwd_);

    // ── Conversations ──────────────────────────────────────────────────────
    conv_handler_ = std::make_shared<ConversationsHandler>(cass_, pg_, jwt_, snowflake_, redis_);
    conv_handler_->register_routes(http_);

    // ── Groups ─────────────────────────────────────────────────────────────
    groups_handler_ = std::make_shared<GroupsHandler>(pg_, jwt_, snowflake_, redis_);
    groups_handler_->register_routes(http_);

    // ── Messages ───────────────────────────────────────────────────────────
    MessagesHandler::AzureBlobConfig blobCfg;
    blobCfg.account         = cfg.azure_storage_account;
    blobCfg.key             = cfg.azure_storage_key;
    blobCfg.container       = cfg.azure_container;
    blobCfg.sas_ttl_minutes = cfg.azure_sas_ttl_min;
    messages_handler_ = std::make_shared<MessagesHandler>(
        cass_, pg_, jwt_, redis_, registry_, snowflake_, std::move(blobCfg));
    messages_handler_->register_routes(http_);

    // ── Users ──────────────────────────────────────────────────────────────
    users_handler_ = std::make_shared<UsersHandler>(pg_, jwt_, redis_);
    users_handler_->register_routes(http_);

    // ── Receipts ───────────────────────────────────────────────────────────
    receipts_handler_ = std::make_shared<ReceiptsHandler>(pg_, jwt_, redis_, registry_);
    receipts_handler_->register_routes(http_);

    // ── Push ───────────────────────────────────────────────────────────────
    push_service_ = std::make_shared<PushService>(pg_);
    push_handler_ = std::make_shared<PushHandler>(pg_, jwt_, push_service_);
    push_handler_->register_routes(http_);

    // ── Docs ──────────────────────────────────────────────────────────────
    std::string spec_json;
    {
        std::ifstream ifs("api/openapi.json");
        if (!ifs.is_open()) {
            LOG_WARN("api/openapi.json not found — /docs will serve a stub spec");
            spec_json = R"({"openapi":"3.0.3","info":{"title":"Loomic API","version":"0.1.0"},"paths":{}})";
        } else {
            std::ostringstream ss;
            ss << ifs.rdbuf();
            spec_json = ss.str();
            LOG_INFO("OpenAPI spec loaded ({} bytes)", spec_json.size());
        }
    }
    register_docs_routes(http_, std::move(spec_json));

    // ── WebSocket upgrade handler ──────────────────────────────────────────
    http_.set_ws_upgrade_handler(
        [this](net::ip::tcp::socket sock,
               boost::beast::flat_buffer buf,
               Request req) -> net::awaitable<void>
        {
            auto session = std::make_shared<WebSocketSession>(
                std::move(sock), registry_, jwt_, redis_,
                cass_, snowflake_, pg_,
                net::ip::host_name() + ":ws");
            co_await session->run(std::move(buf), std::move(req));
        });
}

void Server::run()
{
    LOG_INFO("Loomic Server starting");
    LOG_INFO("Thread pool: {} threads", thread_count_);

    http_.start();
    tcp_.start();

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

    // Main thread drives io_ctxs_[0] (signals + HTTP + TCP)
    io_ctxs_[0].run();

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
