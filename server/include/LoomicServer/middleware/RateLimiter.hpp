#pragma once

#include <chrono>
#include <mutex>
#include <string>

#include <absl/container/flat_hash_map.h>

namespace Loomic {

/// Per-IP token-bucket rate limiter.
/// Thread-safe. Buckets are lazily created on first access.
class RateLimiter {
public:
    /// max_tokens: burst capacity; refill_rate: tokens added per second.
    RateLimiter(double max_tokens, double refill_rate);

    /// Returns true and consumes one token if the key has capacity, else false.
    bool allow(const std::string& key);

private:
    struct Bucket {
        double                                  tokens{0.0};
        std::chrono::steady_clock::time_point   last_refill{};
    };

    double max_tokens_;
    double refill_rate_;
    absl::flat_hash_map<std::string, Bucket> buckets_;
    std::mutex mutex_;
};

} // namespace Loomic
