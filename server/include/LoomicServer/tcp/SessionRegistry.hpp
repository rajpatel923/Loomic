#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>

#include <absl/container/flat_hash_map.h>

namespace Loomic {

class ISession;

/// Thread-safe map from user_id → weak_ptr<ISession>.
/// Readers use a shared lock; writers use an exclusive lock.
/// Accepts any ISession subtype (Session, WebSocketSession, …).
class SessionRegistry {
public:
    void insert(uint64_t user_id, std::shared_ptr<ISession> s);
    std::shared_ptr<ISession> lookup(uint64_t user_id) const;
    void remove(uint64_t user_id, const ISession* expected = nullptr);

private:
    absl::flat_hash_map<uint64_t, std::weak_ptr<ISession>> map_;
    mutable std::shared_mutex                              mutex_;
};

} // namespace Loomic
