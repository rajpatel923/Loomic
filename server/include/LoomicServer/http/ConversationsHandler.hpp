#pragma once

#include <memory>

#include <boost/asio/awaitable.hpp>

#include "LoomicServer/http/HttpServer.hpp"

namespace Loomic {

class CassandraClient;
class PgPool;
class JwtService;
class SnowflakeGen;

class ConversationsHandler {
public:
    ConversationsHandler(std::shared_ptr<CassandraClient> cass,
                         std::shared_ptr<PgPool>          pg,
                         std::shared_ptr<JwtService>      jwt,
                         std::shared_ptr<SnowflakeGen>    snowflake);

    void register_routes(HttpServer& http);

private:
    boost::asio::awaitable<Response> get_messages(const Request& req,
                                                  const PathParams& params);
    boost::asio::awaitable<Response> create_conversation(const Request& req,
                                                         const PathParams& params);
    boost::asio::awaitable<Response> list_conversations(const Request& req,
                                                        const PathParams& params);

    std::shared_ptr<CassandraClient> cass_;
    std::shared_ptr<PgPool>          pg_;
    std::shared_ptr<JwtService>      jwt_;
    std::shared_ptr<SnowflakeGen>    snowflake_;
};

} // namespace Loomic
