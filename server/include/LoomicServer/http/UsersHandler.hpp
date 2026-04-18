#pragma once

#include <memory>

namespace Loomic {

class HttpServer;
class PgPool;
class JwtService;

/// Register GET /users/search on the given HTTP server.
void register_users_routes(HttpServer&                   http,
                           std::shared_ptr<PgPool>       pg,
                           std::shared_ptr<JwtService>   jwt);

} // namespace Loomic
