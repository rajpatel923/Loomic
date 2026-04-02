#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

namespace Loomic {

struct Config {
    // Network
    std::string  bind_address     = "0.0.0.0";
    uint16_t     port             = 7777;
    uint16_t     http_health_port = 8080;
    unsigned int thread_count     = 0;   // 0 = std::thread::hardware_concurrency()

    // TLS
    std::string tls_cert_path;
    std::string tls_key_path;

    // Data stores
    std::string scylla_hosts;
    std::string pg_conn_string;   // required
    std::string redis_host        = "127.0.0.1";
    uint16_t    redis_port        = 6379;

    // Auth
    std::string jwt_secret;       // required

    // Logging
    std::string log_level = "info";
    std::string log_file  = "logs/server.log";

    // Metrics
    uint16_t metrics_port = 9090;

    /// Load from a JSON file (comments allowed). Throws std::runtime_error on
    /// missing required fields or parse errors.
    static Config from_file(const std::filesystem::path& path);

    /// Overlay LOOMIC_* environment variables on top of an existing config.
    static void from_env(Config& cfg);

    /// Parse a .env file and export each KEY=VALUE as a process environment
    /// variable. Variables already set in the environment are NOT overwritten,
    /// so real shell / Docker env vars always take precedence.
    /// Silently does nothing if the file does not exist.
    static void load_dotenv(const std::filesystem::path& path = ".env");
};

} // namespace Loomic
