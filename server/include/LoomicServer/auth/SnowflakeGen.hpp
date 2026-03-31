#pragma once

#include <cstdint>
#include <mutex>

namespace Loomic {

/// Thread-safe 64-bit Snowflake ID generator.
/// Layout: 41 bits timestamp (ms since EPOCH) | 10 bits machine_id | 12 bits sequence
class SnowflakeGen {
public:
    explicit SnowflakeGen(uint16_t machine_id = 1);

    /// Returns the next unique ID. Blocks for < 1ms if the sequence overflows.
    int64_t next();

private:
    // 2024-01-01T00:00:00Z in milliseconds since Unix epoch
    static constexpr int64_t EPOCH_MS = 1704067200000LL;

    uint16_t   machine_id_;
    int64_t    last_ms_{0};
    int64_t    sequence_{0};
    std::mutex mutex_;
};

} // namespace Loomic
