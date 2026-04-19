#pragma once

#include <memory>

namespace Loomic {

class HttpServer;
class CassandraClient;
class PgPool;
class JwtService;
class RedisClient;
class SessionRegistry;
class SnowflakeGen;

class MessagesHandler {
public:
    MessagesHandler(std::shared_ptr<CassandraClient> cass,
                    std::shared_ptr<PgPool>           pg,
                    std::shared_ptr<JwtService>       jwt,
                    std::shared_ptr<RedisClient>      redis,
                    std::shared_ptr<SessionRegistry>  registry,
                    std::shared_ptr<SnowflakeGen>     snowflake);

    void register_routes(HttpServer& http);

private:
    std::shared_ptr<CassandraClient> cass_;
    std::shared_ptr<PgPool>          pg_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<RedisClient>     redis_;
    std::shared_ptr<SessionRegistry> registry_;
    std::shared_ptr<SnowflakeGen>    snowflake_;
};

} // namespace Loomic
