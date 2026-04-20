#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

namespace Loomic {

class PgPool;

class PushService {
public:
    explicit PushService(std::shared_ptr<PgPool> pg);

    boost::asio::awaitable<void> register_token(uint64_t user_id,
                                                 const std::string& token,
                                                 const std::string& platform);

    boost::asio::awaitable<void> unregister_token(uint64_t user_id,
                                                    const std::string& token);

    /// Stub: logs the push notification, no real network call.
    boost::asio::awaitable<void> send_push(uint64_t user_id,
                                            std::string_view title,
                                            std::string_view body);

private:
    std::shared_ptr<PgPool> pg_;
};

} // namespace Loomic
