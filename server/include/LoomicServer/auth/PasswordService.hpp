#pragma once

#include <string>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

namespace Loomic {

/// bcrypt hash/verify executed on a thread_pool (bcrypt is CPU-bound/blocking).
class PasswordService {
public:
    explicit PasswordService(boost::asio::thread_pool& pool);

    /// Hashes plaintext with bcrypt cost 12. Runs on thread_pool.
    boost::asio::awaitable<std::string> hash(std::string plaintext);

    /// Verifies plaintext against a bcrypt hash. Runs on thread_pool.
    boost::asio::awaitable<bool> verify(std::string plaintext, std::string stored_hash);

private:
    boost::asio::thread_pool& pool_;
};

} // namespace Loomic
