#include "LoomicServer/middleware/RateLimiter.hpp"

#include <algorithm>

namespace Loomic {

RateLimiter::RateLimiter(double max_tokens, double refill_rate)
    : max_tokens_(max_tokens), refill_rate_(refill_rate)
{}

bool RateLimiter::allow(const std::string& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    auto& bucket = buckets_[key];

    if (bucket.last_refill.time_since_epoch().count() == 0) {
        // First request for this key: grant it and initialise the bucket.
        bucket.tokens      = max_tokens_ - 1.0;
        bucket.last_refill = now;
        return true;
    }

    // Refill proportional to elapsed time.
    auto elapsed   = std::chrono::duration<double>(now - bucket.last_refill).count();
    bucket.tokens  = std::min(max_tokens_, bucket.tokens + elapsed * refill_rate_);
    bucket.last_refill = now;

    if (bucket.tokens >= 1.0) {
        bucket.tokens -= 1.0;
        return true;
    }
    return false;
}

} // namespace Loomic
