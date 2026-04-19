#pragma once

#include <memory>

namespace Loomic {

class HttpServer;
class PgPool;
class JwtService;
class SnowflakeGen;
class RedisClient;

class GroupsHandler {
public:
    GroupsHandler(std::shared_ptr<PgPool>      pg,
                  std::shared_ptr<JwtService>  jwt,
                  std::shared_ptr<SnowflakeGen> snowflake,
                  std::shared_ptr<RedisClient> redis);

    void register_routes(HttpServer& http);

private:
    std::shared_ptr<PgPool>       pg_;
    std::shared_ptr<JwtService>   jwt_;
    std::shared_ptr<SnowflakeGen> snowflake_;
    std::shared_ptr<RedisClient>  redis_;
};

} // namespace Loomic
