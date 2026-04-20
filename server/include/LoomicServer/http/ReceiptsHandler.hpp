#pragma once

#include <memory>

#include "LoomicServer/http/HttpServer.hpp"

namespace Loomic {

class PgPool;
class JwtService;
class RedisClient;
class SessionRegistry;

class ReceiptsHandler {
public:
    ReceiptsHandler(std::shared_ptr<PgPool>          pg,
                    std::shared_ptr<JwtService>      jwt,
                    std::shared_ptr<RedisClient>     redis,
                    std::shared_ptr<SessionRegistry> registry);

    void register_routes(HttpServer& http);

private:
    std::shared_ptr<PgPool>          pg_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<RedisClient>     redis_;
    std::shared_ptr<SessionRegistry> registry_;
};

} // namespace Loomic
