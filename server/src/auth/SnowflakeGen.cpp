#include "LoomicServer/auth/SnowflakeGen.hpp"

#include <chrono>

namespace Loomic {

SnowflakeGen::SnowflakeGen(uint16_t machine_id)
    : machine_id_(static_cast<uint16_t>(machine_id & 0x3FFu))
{}

int64_t SnowflakeGen::next()
{
    using namespace std::chrono;
    std::lock_guard<std::mutex> lock(mutex_);

    auto now_ms = duration_cast<milliseconds>(
        system_clock::now().time_since_epoch()).count() - EPOCH_MS;

    if (now_ms == last_ms_) {
        ++sequence_;
        if (sequence_ > 0xFFFLL) {
            // Sequence exhausted — spin until the next millisecond.
            while (now_ms == last_ms_) {
                now_ms = duration_cast<milliseconds>(
                    system_clock::now().time_since_epoch()).count() - EPOCH_MS;
            }
            sequence_ = 0;
        }
    } else {
        sequence_ = 0;
        last_ms_  = now_ms;
    }

    return (now_ms << 22)
         | (static_cast<int64_t>(machine_id_) << 12)
         | sequence_;
}

} // namespace Loomic
