#include "LoomicServer/auth/PasswordService.hpp"

#include <crypt.h>
#include <stdexcept>

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

    char salt[CRYPT_GENSALT_OUTPUT_SIZE];
    if (!crypt_gensalt_rn("$2b$", 12, nullptr, 0, salt, sizeof(salt)))
        throw std::runtime_error("crypt_gensalt_rn failed");

    struct crypt_data data{};
    const char* result = crypt_r(plaintext.c_str(), salt, &data);
    if (!result)
        throw std::runtime_error("crypt_r failed");

    co_return std::string(result);
}

net::awaitable<bool> PasswordService::verify(std::string plaintext, std::string stored_hash)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);

    struct crypt_data data{};
    const char* result = crypt_r(plaintext.c_str(), stored_hash.c_str(), &data);
    if (!result) co_return false;

    co_return stored_hash == result;
}

} // namespace Loomic
