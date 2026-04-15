#pragma once

#include <cstdint>
#include <memory>
#include <shared_mutex>

#include <absl/container/flat_hash_map.h>

namespace Loomic {

class Session;

/// Thread-safe map from user_id → weak_ptr<Session>.
/// Readers use a shared lock; writers use an exclusive lock.
class SessionRegistry {
public:
    void insert(uint64_t user_id, std::shared_ptr<Session> s);
    std::shared_ptr<Session> lookup(uint64_t user_id) const;
    void remove(uint64_t user_id);

private:
    absl::flat_hash_map<uint64_t, std::weak_ptr<Session>> map_;
    mutable std::shared_mutex                             mutex_;
};

} // namespace Loomic
