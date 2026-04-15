#include "LoomicServer/tcp/SessionRegistry.hpp"
#include <mutex>

namespace Loomic {

void SessionRegistry::insert(uint64_t user_id, std::shared_ptr<Session> s)
{
    std::unique_lock lock(mutex_);
    map_.insert_or_assign(user_id, std::weak_ptr<Session>(s));
}

std::shared_ptr<Session> SessionRegistry::lookup(uint64_t user_id) const
{
    std::shared_lock lock(mutex_);
    auto it = map_.find(user_id);
    if (it == map_.end()) return nullptr;
    return it->second.lock(); // returns nullptr if Session has been destroyed
}

void SessionRegistry::remove(uint64_t user_id)
{
    std::unique_lock lock(mutex_);
    map_.erase(user_id);
}

} // namespace Loomic
