#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

#include <absl/container/flat_hash_map.h>

namespace Loomic {

/// Singleton wrapper around prometheus-cpp.
/// Call init() once at server startup; then use get() from anywhere.
class MetricsRegistry {
public:
    /// Start the CivetWeb Exposer on the given port (0 = skip Exposer).
    /// Thread-safe; subsequent calls are no-ops (std::call_once).
    static void init(uint16_t port);

    /// Return the global singleton. Throws if init() was not called.
    static MetricsRegistry& get();

    /// Total CHAT messages routed through the server.
    prometheus::Counter& messages_total();

    /// HTTP request counter labelled by method and status code (e.g. "GET", "200").
    /// Label combos are cached in an absl::flat_hash_map to avoid per-request Add() calls.
    prometheus::Counter& http_requests_total(std::string_view method,
                                             std::string_view status);

    /// Number of currently authenticated TCP/WebSocket sessions.
    prometheus::Gauge& active_sessions();

    /// Number of open TCP/WebSocket connections (including unauthenticated).
    prometheus::Gauge& active_connections();

    /// End-to-end message routing latency in ms.
    /// Buckets: 0.1, 1, 5, 10, 50, 100 ms.
    prometheus::Histogram& message_latency_ms();

    /// HTTP request handling latency in ms.
    /// Buckets: 1, 5, 10, 25, 50, 100, 250 ms.
    prometheus::Histogram& http_latency_ms();

private:
    MetricsRegistry() = default;

    std::shared_ptr<prometheus::Registry>      registry_;
    std::unique_ptr<prometheus::Exposer>       exposer_;

    prometheus::Family<prometheus::Counter>*   messages_family_    {nullptr};
    prometheus::Counter*                       messages_counter_   {nullptr};

    prometheus::Family<prometheus::Counter>*   http_req_family_    {nullptr};
    absl::flat_hash_map<std::string,
                        prometheus::Counter*>  http_req_cache_;
    std::mutex                                 http_req_mutex_;

    prometheus::Family<prometheus::Gauge>*     active_sess_family_ {nullptr};
    prometheus::Gauge*                         active_sess_        {nullptr};

    prometheus::Family<prometheus::Gauge>*     active_conn_family_ {nullptr};
    prometheus::Gauge*                         active_conn_        {nullptr};

    prometheus::Family<prometheus::Histogram>* msg_lat_family_     {nullptr};
    prometheus::Histogram*                     msg_lat_            {nullptr};

    prometheus::Family<prometheus::Histogram>* http_lat_family_    {nullptr};
    prometheus::Histogram*                     http_lat_           {nullptr};
};

} // namespace Loomic
