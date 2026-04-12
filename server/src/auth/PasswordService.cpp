#include "LoomicServer/auth/PasswordService.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace net = boost::asio;

namespace {

constexpr int ITERATIONS = 100'000;
constexpr int SALT_LEN   = 16;   // 128-bit salt
constexpr int KEY_LEN    = 32;   // 256-bit derived key

std::string to_hex(const unsigned char* data, int len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < len; ++i)
        oss << std::setw(2) << static_cast<unsigned>(data[i]);
    return oss.str();
}

bool from_hex(const std::string& hex, unsigned char* out, int len)
{
    if (static_cast<int>(hex.size()) != len * 2) return false;
    for (int i = 0; i < len; ++i) {
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nibble(hex[i * 2]), lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<unsigned char>((hi << 4) | lo);
    }
    return true;
}

} // namespace

namespace Loomic {

PasswordService::PasswordService(net::thread_pool& pool)
    : pool_(pool)
{}

net::awaitable<std::string> PasswordService::hash(std::string plaintext)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);

    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1)
        throw std::runtime_error("RAND_bytes failed");

    unsigned char key[KEY_LEN];
    if (PKCS5_PBKDF2_HMAC(
            plaintext.c_str(), static_cast<int>(plaintext.size()),
            salt, SALT_LEN, ITERATIONS,
            EVP_sha256(), KEY_LEN, key) != 1)
        throw std::runtime_error("PKCS5_PBKDF2_HMAC failed");

    co_return "pbkdf2:sha256:" + std::to_string(ITERATIONS) + ":"
            + to_hex(salt, SALT_LEN) + ":"
            + to_hex(key, KEY_LEN);
}

net::awaitable<bool> PasswordService::verify(std::string plaintext, std::string stored_hash)
{
    co_await net::post(pool_.get_executor(), net::use_awaitable);

    // Format: pbkdf2:sha256:<iter>:<hex_salt>:<hex_key>
    constexpr auto npos = std::string::npos;
    size_t p1 = stored_hash.find(':');
    size_t p2 = stored_hash.find(':', p1 + 1);
    size_t p3 = stored_hash.find(':', p2 + 1);
    size_t p4 = stored_hash.find(':', p3 + 1);
    if (p1 == npos || p2 == npos || p3 == npos || p4 == npos)
        co_return false;

    int iterations;
    try { iterations = std::stoi(stored_hash.substr(p2 + 1, p3 - p2 - 1)); }
    catch (...) { co_return false; }

    unsigned char salt[SALT_LEN];
    unsigned char stored_key[KEY_LEN];
    if (!from_hex(stored_hash.substr(p3 + 1, p4 - p3 - 1), salt, SALT_LEN))
        co_return false;
    if (!from_hex(stored_hash.substr(p4 + 1), stored_key, KEY_LEN))
        co_return false;

    unsigned char computed_key[KEY_LEN];
    if (PKCS5_PBKDF2_HMAC(
            plaintext.c_str(), static_cast<int>(plaintext.size()),
            salt, SALT_LEN, iterations,
            EVP_sha256(), KEY_LEN, computed_key) != 1)
        co_return false;

    co_return CRYPTO_memcmp(computed_key, stored_key, KEY_LEN) == 0;
}

} // namespace Loomic
