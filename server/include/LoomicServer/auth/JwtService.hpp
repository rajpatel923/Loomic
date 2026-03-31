#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace Loomic {

struct AuthUser {
    int64_t uid;
};

/// HS256 JWT issue/verify wrapper around jwt-cpp.
class JwtService {
public:
    explicit JwtService(std::string secret);

    /// Issues an HS256 JWT with sub=uid, iss="loomic", iat, exp.
    std::string issue(int64_t uid,
                      std::chrono::seconds ttl = std::chrono::hours(24));

    /// Verifies a JWT and returns the AuthUser, or nullopt if invalid/expired.
    std::optional<AuthUser> verify(std::string_view token) const;

private:
    std::string secret_;
};

} // namespace Loomic
