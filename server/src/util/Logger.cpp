#include "LoomicServer/util/Logger.hpp"
#include "LoomicServer/util/Config.hpp"
#include "LoomicServer/util/RequestContext.hpp"

#include <filesystem>
#include <stdexcept>

#include <spdlog/async.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Loomic {

std::shared_ptr<spdlog::logger> Logger::s_logger;

// Custom flag formatter: injects the thread-local X-Request-ID into %* slots.
// Thread-safe: each ASIO coroutine strand runs on one thread at a time.
struct RequestIdFlag : spdlog::custom_flag_formatter {
    void format(const spdlog::details::log_msg& /*msg*/,
                const std::tm& /*tm*/,
                spdlog::memory_buf_t& dest) override
    {
        const auto& rid = g_request_ctx.request_id;
        dest.append(rid.data(), rid.data() + rid.size());
    }

    std::unique_ptr<spdlog::custom_flag_formatter> clone() const override {
        return spdlog::details::make_unique<RequestIdFlag>();
    }
};

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

    // Stdout color sink (errors only — human-readable in dev)
    auto stdout_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    stdout_sink->set_level(spdlog::level::err);
    sinks.push_back(stdout_sink);

    // Rotating file sink (100 MB, 5 files) — JSON structured output
    if (!cfg.log_file.empty()) {
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            cfg.log_file, 100 * 1024 * 1024, 5);
        file_sink->set_level(spdlog::level::from_str(cfg.log_level));
        sinks.push_back(file_sink);
    }

    s_logger = std::make_shared<spdlog::async_logger>(
        "main",
        sinks.begin(), sinks.end(),
        spdlog::thread_pool(),
        spdlog::async_overflow_policy::discard_new);

    // JSON structured log format with X-Request-ID via custom %* flag.
    // %Y-%m-%dT%H:%M:%S.%e  — ISO-8601 timestamp with milliseconds
    // %t                     — thread ID
    // %l                     — level string
    // %*                     — custom: thread-local request ID
    // %v                     — log message
    auto formatter = std::make_unique<spdlog::pattern_formatter>();
    formatter->add_flag<RequestIdFlag>('*');
    formatter->set_pattern(
        R"({"ts":"%Y-%m-%dT%H:%M:%S.%e","tid":%t,"lvl":"%l","rid":"%*","msg":"%v"})");
    s_logger->set_formatter(std::move(formatter));

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
