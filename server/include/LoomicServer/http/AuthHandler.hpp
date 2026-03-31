#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "LoomicServer/auth/JwtService.hpp"

namespace Loomic {

class HttpServer;
class PgPool;
class SnowflakeGen;
class PasswordService;

/// Register /auth/register, /auth/login, and /auth/refresh routes on server.
void register_auth_routes(HttpServer& server,
                          std::shared_ptr<PgPool>          pg,
                          std::shared_ptr<SnowflakeGen>    snowflake,
                          std::shared_ptr<JwtService>      jwt,
                          std::shared_ptr<PasswordService> pwd);

/// Extract and verify a Bearer token from an Authorization header.
/// Returns nullopt if the header is missing, malformed, or the JWT is invalid.
std::optional<AuthUser> require_auth(std::string_view authorization_header,
                                     const JwtService& jwt);

} // namespace Loomic
