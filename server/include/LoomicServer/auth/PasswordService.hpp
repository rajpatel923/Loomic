#pragma once

#include <string>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

namespace Loomic {

/// PBKDF2-SHA256 hash/verify executed on a thread_pool (CPU-bound/blocking).
class PasswordService {
public:
    explicit PasswordService(boost::asio::thread_pool& pool);

    /// Hashes plaintext with PBKDF2-SHA256 (100k iterations, 128-bit salt). Runs on thread_pool.
    boost::asio::awaitable<std::string> hash(std::string plaintext);

    /// Verifies plaintext against a PBKDF2-SHA256 hash. Runs on thread_pool.
    boost::asio::awaitable<bool> verify(std::string plaintext, std::string stored_hash);

private:
    boost::asio::thread_pool& pool_;
};

} // namespace Loomic
