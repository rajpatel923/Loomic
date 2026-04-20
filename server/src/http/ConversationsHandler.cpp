#include "LoomicServer/http/ConversationsHandler.hpp"
#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/util/Logger.hpp"

#include "chat.pb.h"

#include <boost/beast/http.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include <charconv>
#include <climits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace net  = boost::asio;
namespace http = boost::beast::http;

namespace Loomic {

namespace {

std::string base64_encode(std::string_view data)
{
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), static_cast<int>(data.size()));
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

std::unordered_map<std::string, std::string> parse_query(std::string_view target)
{
    std::unordered_map<std::string, std::string> result;
    auto qpos = target.find('?');
    if (qpos == std::string_view::npos) return result;
    auto qs = target.substr(qpos + 1);
    while (!qs.empty()) {
        auto amp  = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        auto eq   = pair.find('=');
        if (eq != std::string_view::npos) {
            result.emplace(std::string(pair.substr(0, eq)),
                           std::string(pair.substr(eq + 1)));
        }
        if (amp == std::string_view::npos) break;
        qs = qs.substr(amp + 1);
    }
    return result;
}

} // namespace

ConversationsHandler::ConversationsHandler(std::shared_ptr<CassandraClient> cass,
                                           std::shared_ptr<PgPool>          pg,
                                           std::shared_ptr<JwtService>      jwt,
                                           std::shared_ptr<SnowflakeGen>    snowflake,
                                           std::shared_ptr<RedisClient>     redis)
    : cass_(std::move(cass))
    , pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , snowflake_(std::move(snowflake))
    , redis_(std::move(redis))
{}

void ConversationsHandler::register_routes(HttpServer& http)
{
    // POST /conversations
    http.add_route(http::verb::post, "/conversations",
        [this](const Request& req, const PathParams& params)
            -> net::awaitable<Response>
        {
            co_return co_await create_conversation(req, params);
        });

    // GET /conversations
    http.add_route(http::verb::get, "/conversations",
        [this](const Request& req, const PathParams& params)
            -> net::awaitable<Response>
        {
            co_return co_await list_conversations(req, params);
        });

    // GET /conversations/{id}/messages
    http.add_route(http::verb::get, "/conversations/{id}/messages",
        [this](const Request& req, const PathParams& params)
            -> net::awaitable<Response>
        {
            co_return co_await get_messages(req, params);
        });
}

net::awaitable<Response>
ConversationsHandler::create_conversation(const Request& req, const PathParams&)
{
    // 1. Auth
    auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
    if (!user) {
        co_return make_error(http::status::forbidden, "missing or invalid token");
    }

    // 2. Parse body
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body());
    } catch (...) {
        co_return make_error(http::status::bad_request, "invalid JSON");
    }

    auto member_ids_j = body.value("member_ids", nlohmann::json::array());
    if (!member_ids_j.is_array() || member_ids_j.empty()) {
        co_return make_error(http::status::bad_request,
                             "member_ids must be a non-empty array");
    }

    // 3. Collect members (creator + requested members)
    auto creator_id = static_cast<uint64_t>(user->uid);
    std::vector<uint64_t> members;
    members.push_back(creator_id);

    for (const auto& id_j : member_ids_j) {
        if (!id_j.is_string()) {
            co_return make_error(http::status::bad_request, "member_ids must be strings");
        }
        auto id_str = id_j.get<std::string>();
        uint64_t mid = 0;
        auto [p, ec] = std::from_chars(id_str.data(), id_str.data() + id_str.size(), mid);
        if (ec != std::errc{}) {
            co_return make_error(http::status::bad_request, "invalid member_id");
        }
        members.push_back(mid);
    }

    // 4. Generate Snowflake conv_id and persist
    auto conv_id = static_cast<uint64_t>(snowflake_->next());
    std::string created_at_str;

    try {
        auto result = co_await pg_->execute(
            [conv_id, members](pqxx::connection& conn) {
                pqxx::work txn(conn);
                auto r = txn.exec_params(
                    "INSERT INTO conversations (id) VALUES ($1) RETURNING created_at",
                    static_cast<int64_t>(conv_id));
                for (auto uid : members) {
                    txn.exec_params(
                        "INSERT INTO conv_members (conv_id, user_id) VALUES ($1, $2) "
                        "ON CONFLICT DO NOTHING",
                        static_cast<int64_t>(conv_id),
                        static_cast<int64_t>(uid));
                }
                txn.commit();
                return r;
            },
            PgPool::RetryClass::NonRetryableWrite);

        if (!result.empty()) {
            created_at_str = result[0][0].as<std::string>();
        }
    } catch (const std::exception& e) {
        LOG_ERROR("create_conversation: {}", e.what());
        co_return make_error(http::status::internal_server_error, "database error");
    }

    nlohmann::json resp;
    resp["conv_id"]    = std::to_string(conv_id);
    resp["created_at"] = created_at_str;
    co_return make_json(http::status::created, resp.dump());
}

net::awaitable<Response>
ConversationsHandler::list_conversations(const Request& req, const PathParams&)
{
    // 1. Auth
    auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
    if (!user) {
        co_return make_error(http::status::forbidden, "missing or invalid token");
    }

    auto user_id = static_cast<uint64_t>(user->uid);

    try {
        // 2. UNION ALL: DMs + groups, sorted by last_activity_at DESC
        auto rows = co_await pg_->execute(
            [user_id](pqxx::connection& conn) {
                pqxx::nontransaction ntxn(conn);
                return ntxn.exec_params(R"(
                    SELECT 'dm'::TEXT, c.id, c.last_activity_at::TEXT, c.last_msg_preview,
                           u.id, u.username, u.bio, u.avatar_url,
                           NULL::TEXT, NULL::TEXT
                    FROM conv_members cm
                    JOIN conversations c   ON c.id = cm.conv_id
                    JOIN conv_members  ocm ON ocm.conv_id = cm.conv_id AND ocm.user_id != $1
                    JOIN users u           ON u.id = ocm.user_id
                    WHERE cm.user_id = $1

                    UNION ALL

                    SELECT 'group'::TEXT, g.id, g.last_activity_at::TEXT, g.last_msg_preview,
                           NULL::BIGINT, NULL::TEXT, NULL::TEXT, NULL::TEXT,
                           g.name, g.avatar_url
                    FROM group_members gm
                    JOIN groups g ON g.id = gm.group_id
                    WHERE gm.user_id = $1

                    ORDER BY 3 DESC NULLS LAST
                )", static_cast<int64_t>(user_id));
            },
            PgPool::RetryClass::ReadOnly);

        // 3. Batch fetch unread counts from Redis (one HGETALL)
        auto unread_map = co_await redis_->hgetall_unread(user_id);

        // 4. Build JSON array
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& row : rows) {
            nlohmann::json item;
            auto kind   = row[0].as<std::string>();
            auto id     = static_cast<uint64_t>(row[1].as<int64_t>());
            item["kind"]             = kind;
            item["id"]               = std::to_string(id);
            item["last_activity_at"] = row[2].is_null() ? "" : row[2].as<std::string>();
            item["last_msg_preview"] = row[3].is_null() ? "" : row[3].as<std::string>();
            item["unread_count"]     = unread_map.count(id) ? unread_map.at(id) : 0;
            if (kind == "dm") {
                item["peer_id"]     = std::to_string(static_cast<uint64_t>(row[4].as<int64_t>()));
                item["peer_name"]   = row[5].as<std::string>();
                item["peer_bio"]    = row[6].is_null() ? "" : row[6].as<std::string>();
                item["peer_avatar"] = row[7].is_null() ? "" : row[7].as<std::string>();
            } else {
                item["group_name"]   = row[8].as<std::string>();
                item["group_avatar"] = row[9].is_null() ? "" : row[9].as<std::string>();
            }
            arr.push_back(std::move(item));
        }

        co_return make_json(http::status::ok, arr.dump());
    } catch (const std::exception& e) {
        LOG_ERROR("list_conversations: {}", e.what());
        co_return make_error(http::status::internal_server_error, "database error");
    }
}

net::awaitable<Response>
ConversationsHandler::get_messages(const Request& req, const PathParams& params)
{
    // 1. Auth
    auto user = require_auth(std::string(req[http::field::authorization]), *jwt_);
    if (!user) {
        co_return make_error(http::status::forbidden, "missing or invalid token");
    }

    // 2. Parse conv_id
    auto id_it = params.find("id");
    if (id_it == params.end()) {
        co_return make_error(http::status::bad_request, "missing conv_id");
    }
    uint64_t conv_id = 0;
    {
        auto [p, ec] = std::from_chars(id_it->second.data(),
                                       id_it->second.data() + id_it->second.size(),
                                       conv_id);
        if (ec != std::errc{}) {
            co_return make_error(http::status::bad_request, "invalid conv_id");
        }
    }

    // 3. Parse query params
    auto qparams = parse_query(std::string_view(req.target()));

    uint64_t before = static_cast<uint64_t>(INT64_MAX);
    if (auto it = qparams.find("before"); it != qparams.end()) {
        std::from_chars(it->second.data(),
                        it->second.data() + it->second.size(), before);
    }

    uint32_t limit = 50;
    if (auto it = qparams.find("limit"); it != qparams.end()) {
        uint32_t parsed = 50;
        auto [p, ec] = std::from_chars(it->second.data(),
                                        it->second.data() + it->second.size(), parsed);
        if (ec == std::errc{}) {
            limit = std::clamp(parsed, 1u, 100u);
        }
    }

    // 4. Membership check (DM conv_members OR group_members)
    auto user_id = static_cast<uint64_t>(user->uid);
    try {
        bool is_member = false;

        // 4a. Check DM membership
        auto dm_row = co_await pg_->execute(
            [conv_id, user_id](pqxx::connection& conn) {
                pqxx::nontransaction ntxn(conn);
                return ntxn.exec_params(
                    "SELECT 1 FROM conv_members WHERE conv_id=$1 AND user_id=$2",
                    static_cast<int64_t>(conv_id),
                    static_cast<int64_t>(user_id));
            },
            PgPool::RetryClass::ReadOnly);
        if (!dm_row.empty()) is_member = true;

        // 4b. Fallback: group membership (group_id stored as conv_id)
        if (!is_member) {
            auto grp_row = co_await pg_->execute(
                [conv_id, user_id](pqxx::connection& conn) {
                    pqxx::nontransaction ntxn(conn);
                    return ntxn.exec_params(
                        "SELECT 1 FROM group_members WHERE group_id=$1 AND user_id=$2",
                        static_cast<int64_t>(conv_id),
                        static_cast<int64_t>(user_id));
                },
                PgPool::RetryClass::ReadOnly);
            if (!grp_row.empty()) is_member = true;
        }

        if (!is_member) {
            co_return make_error(http::status::forbidden,
                                 "not a member of this conversation");
        }
    } catch (const std::exception& e) {
        LOG_ERROR("get_messages membership check: {}", e.what());
        co_return make_error(http::status::internal_server_error, "database error");
    }

    // 5. Fetch history
    auto messages = co_await cass_->select_history(conv_id, before, limit);

    // 6. Serialize — all Snowflake IDs as strings to avoid JS precision loss
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& msg : messages) {
        nlohmann::json item;
        item["msg_id"]       = std::to_string(msg.msg_id());
        item["sender_id"]    = std::to_string(msg.sender_id());
        item["recipient_id"] = std::to_string(msg.recipient_id());
        item["content_b64"]  = base64_encode(msg.content());
        item["msg_type"]     = static_cast<int>(msg.type());
        item["ts_ms"]        = msg.timestamp_ms();
        arr.push_back(std::move(item));
    }

    co_return make_json(http::status::ok, arr.dump());
}

} // namespace Loomic
