#pragma once

#include <memory>
#include <string>

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
    struct AzureBlobConfig {
        std::string account;
        std::string key;       // base64 storage account key
        std::string container;
        int         sas_ttl_minutes = 60;
    };

    MessagesHandler(std::shared_ptr<CassandraClient> cass,
                    std::shared_ptr<PgPool>           pg,
                    std::shared_ptr<JwtService>       jwt,
                    std::shared_ptr<RedisClient>      redis,
                    std::shared_ptr<SessionRegistry>  registry,
                    std::shared_ptr<SnowflakeGen>     snowflake,
                    AzureBlobConfig                   blob_cfg);

    void register_routes(HttpServer& http);

private:
    std::shared_ptr<CassandraClient> cass_;
    std::shared_ptr<PgPool>          pg_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<RedisClient>     redis_;
    std::shared_ptr<SessionRegistry> registry_;
    std::shared_ptr<SnowflakeGen>    snowflake_;
    AzureBlobConfig                  blob_cfg_;
};

} // namespace Loomic
