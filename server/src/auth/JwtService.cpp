#include "LoomicServer/auth/JwtService.hpp"

#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include <stdexcept>

namespace Loomic {

JwtService::JwtService(std::string secret)
    : secret_(std::move(secret))
{}

std::string JwtService::issue(int64_t uid, std::chrono::seconds ttl)
{
    auto now = std::chrono::system_clock::now();
    return jwt::create()
        .set_issuer("loomic")
        .set_subject(std::to_string(uid))
        .set_issued_at(now)
        .set_expires_at(now + ttl)
        .sign(jwt::algorithm::hs256{secret_});
}

std::optional<AuthUser> JwtService::verify(std::string_view token) const
{
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{secret_})
            .with_issuer("loomic");

        auto decoded = jwt::decode(std::string(token));
        verifier.verify(decoded);

        int64_t uid = std::stoll(decoded.get_subject());
        return AuthUser{uid};
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace Loomic
