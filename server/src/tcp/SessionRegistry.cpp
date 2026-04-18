#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/tcp/ISession.hpp"
#include <mutex>

namespace Loomic {

void SessionRegistry::insert(uint64_t user_id, std::shared_ptr<ISession> s)
{
    std::unique_lock lock(mutex_);
    map_.insert_or_assign(user_id, std::weak_ptr<ISession>(s));
}

std::shared_ptr<ISession> SessionRegistry::lookup(uint64_t user_id) const
{
    std::shared_lock lock(mutex_);
    auto it = map_.find(user_id);
    if (it == map_.end()) return nullptr;
    return it->second.lock(); // returns nullptr if session has been destroyed
}

void SessionRegistry::remove(uint64_t user_id, const ISession* expected)
{
    std::unique_lock lock(mutex_);
    auto it = map_.find(user_id);
    if (it == map_.end()) {
        return;
    }

    if (!expected) {
        map_.erase(it);
        return;
    }

    auto current = it->second.lock();
    if (current && current.get() == expected) {
        map_.erase(it);
    }
}

} // namespace Loomic
