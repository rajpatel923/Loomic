#pragma once

#include <string>

namespace Loomic {

/// Per-coroutine tracing context stored in thread-local storage.
/// Safe: each Boost.ASIO coroutine strand executes on one thread at a time,
/// so the thread-local is effectively per-coroutine while the strand is running.
struct RequestContext {
    std::string request_id;
};

extern thread_local RequestContext g_request_ctx;

} // namespace Loomic
