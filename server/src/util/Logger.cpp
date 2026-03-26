#include "LoomicServer/util/Logger.hpp"
#include "LoomicServer/util/Config.hpp"

#include <filesystem>
#include <stdexcept>

#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Loomic {

std::shared_ptr<spdlog::logger> Logger::s_logger;

void Logger::init(const Config& cfg)
{
    // Ensure log directory exists
    if (!cfg.log_file.empty()) {
        std::filesystem::path log_path(cfg.log_file);
        if (log_path.has_parent_path()) {
            std::filesystem::create_directories(log_path.parent_path());
        }
    }

    // Async thread pool: 65536-slot queue, 1 background thread
    spdlog::init_thread_pool(65536, 1);

    std::vector<spdlog::sink_ptr> sinks;

    // Stdout color sink
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    sinks.push_back(stdout_sink);

    // Rotating file sink (100 MB, 5 files)
    if (!cfg.log_file.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.log_file, 100 * 1024 * 1024, 5);
        sinks.push_back(file_sink);
    }

    s_logger = std::make_shared<spdlog::async_logger>(
        "main",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::discard_new);

    s_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][%t][%l] %v");

    // Parse log level
    auto level = spdlog::level::from_str(cfg.log_level);
    if (level == spdlog::level::off && cfg.log_level != "off") {
        level = spdlog::level::info;
    }
    s_logger->set_level(level);

    spdlog::register_logger(s_logger);
    spdlog::set_default_logger(s_logger);
}

std::shared_ptr<spdlog::logger> Logger::get()
{
    if (!s_logger) {
        throw std::runtime_error("Logger::init() must be called before Logger::get()");
    }
    return s_logger;
}

} // namespace Loomic
