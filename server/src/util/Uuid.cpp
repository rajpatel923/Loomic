#include "LoomicServer/util/Uuid.hpp"
#include "LoomicServer/util/RequestContext.hpp"

#include <cstdio>
#include <random>

namespace Loomic {

// Definition of the thread-local request context (declared extern in RequestContext.hpp).
thread_local RequestContext g_request_ctx;

std::string generate_uuid_v4()
{
    thread_local std::mt19937_64 rng{std::random_device{}()};

    uint64_t hi = rng();
    uint64_t lo = rng();

    // Set version 4: bits [15:12] of hi = 0100
    hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    // Set variant bits 10xx: bits [63:62] of lo = 10
    lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%012llx",
        static_cast<unsigned int>(hi >> 32),
        static_cast<unsigned int>((hi >> 16) & 0xFFFFU),
        static_cast<unsigned int>(hi & 0xFFFFU),
        static_cast<unsigned int>(lo >> 48),
        static_cast<unsigned long long>(lo & 0x0000FFFFFFFFFFFFULL));

    return buf;
}

} // namespace Loomic
