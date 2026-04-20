#pragma once

#include <memory>

#include "LoomicServer/http/HttpServer.hpp"

namespace Loomic {

class PgPool;
class JwtService;
class PushService;

class PushHandler {
public:
    PushHandler(std::shared_ptr<PgPool>      pg,
                std::shared_ptr<JwtService>  jwt,
                std::shared_ptr<PushService> push_service);

    void register_routes(HttpServer& http);

private:
    std::shared_ptr<PgPool>      pg_;
    std::shared_ptr<JwtService>  jwt_;
    std::shared_ptr<PushService> push_service_;
};

} // namespace Loomic
