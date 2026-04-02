#include "LoomicServer/util/Config.hpp"

#include <fstream>
#include <stdexcept>
#include <cstdlib>
#include <string>

#include <nlohmann/json.hpp>

namespace Loomic {

Config Config::from_file(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path.string());
    }

    nlohmann::json j = nlohmann::json::parse(ifs, /*cb=*/nullptr, /*allow_exceptions=*/true,
                                              /*ignore_comments=*/true);

    Config cfg;

    // Network
    if (j.contains("bind_address"))     cfg.bind_address     = j["bind_address"].get<std::string>();
    if (j.contains("port"))             cfg.port             = j["port"].get<uint16_t>();
    if (j.contains("http_health_port")) cfg.http_health_port = j["http_health_port"].get<uint16_t>();
    if (j.contains("thread_count"))     cfg.thread_count     = j["thread_count"].get<unsigned int>();

    // TLS
    if (j.contains("tls_cert_path")) cfg.tls_cert_path = j["tls_cert_path"].get<std::string>();
    if (j.contains("tls_key_path"))  cfg.tls_key_path  = j["tls_key_path"].get<std::string>();

    // Data stores
    if (j.contains("scylla_hosts")) cfg.scylla_hosts = j["scylla_hosts"].get<std::string>();
    if (j.contains("redis_host"))   cfg.redis_host   = j["redis_host"].get<std::string>();
    if (j.contains("redis_port"))   cfg.redis_port   = j["redis_port"].get<uint16_t>();

    // Required fields
    if (!j.contains("pg_conn_string") || j["pg_conn_string"].get<std::string>().empty()) {
        throw std::runtime_error("Config: missing required field 'pg_conn_string'");
    }
    cfg.pg_conn_string = j["pg_conn_string"].get<std::string>();

    if (!j.contains("jwt_secret") || j["jwt_secret"].get<std::string>().empty()) {
        throw std::runtime_error("Config: missing required field 'jwt_secret'");
    }
    cfg.jwt_secret = j["jwt_secret"].get<std::string>();

    // Logging
    if (j.contains("log_level")) cfg.log_level = j["log_level"].get<std::string>();
    if (j.contains("log_file"))  cfg.log_file  = j["log_file"].get<std::string>();

    // Metrics
    if (j.contains("metrics_port")) cfg.metrics_port = j["metrics_port"].get<uint16_t>();

    return cfg;
}

void Config::from_env(Config& cfg)
{
    auto get = [](const char* name) -> std::string {
        const char* val = std::getenv(name);
        return val ? std::string(val) : std::string{};
    };

    if (auto v = get("LOOMIC_BIND_ADDRESS");     !v.empty()) cfg.bind_address     = v;
    if (auto v = get("LOOMIC_PORT");             !v.empty()) cfg.port             = static_cast<uint16_t>(std::stoi(v));
    if (auto v = get("LOOMIC_HTTP_HEALTH_PORT"); !v.empty()) cfg.http_health_port = static_cast<uint16_t>(std::stoi(v));
    if (auto v = get("LOOMIC_THREAD_COUNT");     !v.empty()) cfg.thread_count     = static_cast<unsigned>(std::stoi(v));
    if (auto v = get("LOOMIC_PG_CONN_STRING");   !v.empty()) cfg.pg_conn_string   = v;
    if (auto v = get("LOOMIC_JWT_SECRET");       !v.empty()) cfg.jwt_secret       = v;
    if (auto v = get("LOOMIC_LOG_LEVEL");        !v.empty()) cfg.log_level        = v;
    if (auto v = get("LOOMIC_LOG_FILE");         !v.empty()) cfg.log_file         = v;
    if (auto v = get("LOOMIC_REDIS_HOST");       !v.empty()) cfg.redis_host       = v;
    if (auto v = get("LOOMIC_REDIS_PORT");       !v.empty()) cfg.redis_port       = static_cast<uint16_t>(std::stoi(v));
    if (auto v = get("LOOMIC_METRICS_PORT");     !v.empty()) cfg.metrics_port     = static_cast<uint16_t>(std::stoi(v));
    if (auto v = get("LOOMIC_SCYLLA_HOSTS");     !v.empty()) cfg.scylla_hosts     = v;
}

void Config::load_dotenv(const std::filesystem::path& path)
{
    std::ifstream ifs(path);
    if (!ifs.is_open()) return;   // file is optional

    std::string line;
    while (std::getline(ifs, line)) {
        // Strip trailing \r (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Skip blank lines and comments
        if (line.empty() || line.front() == '#') continue;

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = line.substr(0, eq);
        std::string value = line.substr(eq + 1);

        // Strip inline comments from value (everything after unquoted ' #')
        // Trim surrounding whitespace from key
        auto ltrim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
        };
        auto rtrim = [](std::string& s) {
            auto pos = s.find_last_not_of(" \t");
            if (pos != std::string::npos) s.erase(pos + 1);
        };
        ltrim(key); rtrim(key);
        ltrim(value); rtrim(value);

        // Strip optional surrounding quotes from value (" or ')
        if (value.size() >= 2 &&
            ((value.front() == '"'  && value.back() == '"') ||
             (value.front() == '\'' && value.back() == '\''))) {
            value = value.substr(1, value.size() - 2);
        }

        if (key.empty()) continue;

        // setenv with overwrite=0: real environment variables take precedence
        ::setenv(key.c_str(), value.c_str(), /*overwrite=*/0);
    }
}

} // namespace Loomic
