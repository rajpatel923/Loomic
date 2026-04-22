#pragma once

#include <memory>
#include <spdlog/spdlog.h>

namespace Loomic {

struct Config;

class Logger {
public:
    /// Initialize the async logger (rotating file + stdout color).
    /// Must be called once before any LOG_* macros are used.
    static void init(const Config& cfg);

    /// Returns the shared logger instance.
    static std::shared_ptr<spdlog::logger> get();

private:
    static std::shared_ptr<spdlog::logger> s_logger;
};

} // namespace Loomic

// Convenience macros — forward to the singleton logger.
#define LOG_DEBUG(...) ::Loomic::Logger::get()->debug(__VA_ARGS__)
#define LOG_INFO(...)  ::Loomic::Logger::get()->info(__VA_ARGS__)
#define LOG_WARN(...)  ::Loomic::Logger::get()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::Loomic::Logger::get()->error(__VA_ARGS__)
