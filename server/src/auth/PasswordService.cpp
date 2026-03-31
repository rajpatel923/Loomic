#include "LoomicServer/auth/PasswordService.hpp"

#include <bcrypt/BCrypt.hpp>

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace net = boost::asio;

namespace Loomic {

PasswordService::PasswordService(net::thread_pool& pool)
    : pool_(pool)
{}

net::awaitable<std::string> PasswordService::hash(std::string plaintext)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);
    co_return BCrypt::generateHash(plaintext, 12);
}

net::awaitable<bool> PasswordService::verify(std::string plaintext, std::string stored_hash)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);
    co_return BCrypt::validatePassword(plaintext, stored_hash);
}

} // namespace Loomic
