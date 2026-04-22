#include "LoomicServer/metrics/MetricsRegistry.hpp"

#include <mutex>
#include <stdexcept>
#include <string>

namespace Loomic {

static MetricsRegistry* s_instance = nullptr;

void MetricsRegistry::init(uint16_t port)
{
    static std::once_flag  init_flag;
    static MetricsRegistry instance;

    std::call_once(init_flag, [&]() {
        instance.registry_ = std::make_shared<prometheus::Registry>();

        // ── messages_total ────────────────────────────────────────────────────
        instance.messages_family_ = &prometheus::BuildCounter()
            .Name("loomic_messages_total")
            .Help("Total number of chat messages routed through the server")
            .Register(*instance.registry_);
        instance.messages_counter_ = &instance.messages_family_->Add({});

        // ── http_requests_total (labelled — cache-backed) ─────────────────────
        instance.http_req_family_ = &prometheus::BuildCounter()
            .Name("loomic_http_requests_total")
            .Help("Total HTTP requests labelled by method and HTTP status code")
            .Register(*instance.registry_);

        // ── active_sessions ───────────────────────────────────────────────────
        instance.active_sess_family_ = &prometheus::BuildGauge()
            .Name("loomic_active_sessions")
            .Help("Number of currently authenticated TCP/WebSocket sessions")
            .Register(*instance.registry_);
        instance.active_sess_ = &instance.active_sess_family_->Add({});

        // ── active_connections ────────────────────────────────────────────────
        instance.active_conn_family_ = &prometheus::BuildGauge()
            .Name("loomic_active_connections")
            .Help("Number of open TCP/WebSocket connections (including unauthenticated)")
            .Register(*instance.registry_);
        instance.active_conn_ = &instance.active_conn_family_->Add({});

        // ── message_latency_ms ────────────────────────────────────────────────
        instance.msg_lat_family_ = &prometheus::BuildHistogram()
            .Name("loomic_message_latency_ms")
            .Help("End-to-end chat message routing latency in milliseconds")
            .Register(*instance.registry_);
        instance.msg_lat_ = &instance.msg_lat_family_->Add(
            {}, prometheus::Histogram::BucketBoundaries{0.1, 1.0, 5.0, 10.0, 50.0, 100.0});

        // ── http_latency_ms ───────────────────────────────────────────────────
        instance.http_lat_family_ = &prometheus::BuildHistogram()
            .Name("loomic_http_latency_ms")
            .Help("HTTP request handling latency in milliseconds")
            .Register(*instance.registry_);
        instance.http_lat_ = &instance.http_lat_family_->Add(
            {}, prometheus::Histogram::BucketBoundaries{1.0, 5.0, 10.0, 25.0, 50.0, 100.0, 250.0});

        // ── Exposer (CivetWeb pull endpoint) ──────────────────────────────────
        if (port > 0) {
            try {
                instance.exposer_ = std::make_unique<prometheus::Exposer>(
                    "0.0.0.0:" + std::to_string(port));
                instance.exposer_->RegisterCollectable(instance.registry_);
            } catch (...) {
                // Non-fatal: metrics still collected, just not exposed
            }
        }

        s_instance = &instance;
    });
}

MetricsRegistry& MetricsRegistry::get()
{
    if (!s_instance) {
        throw std::runtime_error("MetricsRegistry::init() must be called before get()");
    }
    return *s_instance;
}

prometheus::Counter& MetricsRegistry::messages_total()
{
    return *messages_counter_;
}

prometheus::Counter& MetricsRegistry::http_requests_total(std::string_view method,
                                                           std::string_view status)
{
    std::string key;
    key.reserve(method.size() + 1 + status.size());
    key.append(method);
    key += ':';
    key.append(status);

    std::lock_guard<std::mutex> lk(http_req_mutex_);
    auto it = http_req_cache_.find(key);
    if (it != http_req_cache_.end()) {
        return *it->second;
    }
    auto& counter = http_req_family_->Add({
        {"method", std::string(method)},
        {"status", std::string(status)}
    });
    http_req_cache_.emplace(std::move(key), &counter);
    return counter;
}

prometheus::Gauge& MetricsRegistry::active_sessions()
{
    return *active_sess_;
}

prometheus::Gauge& MetricsRegistry::active_connections()
{
    return *active_conn_;
}

prometheus::Histogram& MetricsRegistry::message_latency_ms()
{
    return *msg_lat_;
}

prometheus::Histogram& MetricsRegistry::http_latency_ms()
{
    return *http_lat_;
}

} // namespace Loomic
