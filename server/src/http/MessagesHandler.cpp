#include "LoomicServer/http/MessagesHandler.hpp"
#include "LoomicServer/http/HttpServer.hpp"
#include "LoomicServer/http/AuthHandler.hpp"
#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/db/PgPool.hpp"
#include "LoomicServer/db/RedisClient.hpp"
#include "LoomicServer/auth/JwtService.hpp"
#include "LoomicServer/auth/SnowflakeGen.hpp"
#include "LoomicServer/tcp/SessionRegistry.hpp"
#include "LoomicServer/tcp/ISession.hpp"
#include "LoomicServer/tcp/frame.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <boost/beast/http.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>

#include <nlohmann/json.hpp>
#include <pqxx/pqxx>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace net  = boost::asio;
namespace http = boost::beast::http;
namespace fs   = std::filesystem;

namespace Loomic {

namespace {

// ── helpers ───────────────────────────────────────────────────────────────────

std::string_view query_param(std::string_view target, std::string_view key)
{
    auto qpos = target.find('?');
    if (qpos == std::string_view::npos) return {};
    auto qs = target.substr(qpos + 1);
    while (!qs.empty()) {
        auto amp  = qs.find('&');
        auto pair = (amp == std::string_view::npos) ? qs : qs.substr(0, amp);
        auto eq   = pair.find('=');
        if (eq != std::string_view::npos && pair.substr(0, eq) == key) {
            return pair.substr(eq + 1);
        }
        if (amp == std::string_view::npos) break;
        qs = qs.substr(amp + 1);
    }
    return {};
}

std::string generate_uuid()
{
    std::random_device              rd;
    std::mt19937_64                 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%08x%08x%08x%08x",
                  dis(gen), dis(gen), dis(gen), dis(gen));
    return std::string(buf);
}

std::string content_type_for(const std::string& filename)
{
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "application/octet-stream";
    auto ext = filename.substr(dot);
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png")  return "image/png";
    if (ext == ".gif")  return "image/gif";
    if (ext == ".pdf")  return "application/pdf";
    if (ext == ".txt")  return "text/plain";
    if (ext == ".json") return "application/json";
    if (ext == ".mp4")  return "video/mp4";
    return "application/octet-stream";
}

// Extract extension (including the dot) from a filename string, e.g. "test.jpg" → ".jpg".
// Returns empty string if none found.
std::string extract_extension(const std::string& filename)
{
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return "";
    return filename.substr(dot);
}

// Minimal multipart/form-data parser.
// Returns {filename, file_bytes} for the first file part found.
std::optional<std::pair<std::string, std::vector<uint8_t>>>
parse_multipart(const std::string& body, const std::string& boundary)
{
    std::string delim = "--" + boundary;
    auto start = body.find(delim);
    if (start == std::string::npos) return std::nullopt;
    start += delim.size();
    // Skip CRLF after boundary
    if (start < body.size() && body[start] == '\r') start++;
    if (start < body.size() && body[start] == '\n') start++;

    // Find end of part headers (blank CRLF line)
    auto headers_end = body.find("\r\n\r\n", start);
    if (headers_end == std::string::npos) return std::nullopt;

    auto headers_str = body.substr(start, headers_end - start);

    // Extract filename from Content-Disposition header
    std::string filename;
    {
        // Case-insensitive search for "filename="
        auto lower_headers = headers_str;
        std::transform(lower_headers.begin(), lower_headers.end(),
                       lower_headers.begin(), [](unsigned char c){ return std::tolower(c); });
        auto fn_pos = lower_headers.find("filename=\"");
        if (fn_pos != std::string::npos) {
            fn_pos += 10; // len("filename=\"")
            auto fn_end = headers_str.find('"', fn_pos);
            if (fn_end != std::string::npos) {
                filename = headers_str.substr(fn_pos, fn_end - fn_pos);
            }
        }
    }

    // File data starts after the blank line
    auto data_start = headers_end + 4; // skip "\r\n\r\n"

    // Find closing boundary (prefixed with \r\n)
    std::string end_delim = "\r\n" + delim;
    auto data_end = body.find(end_delim, data_start);
    if (data_end == std::string::npos) {
        data_end = body.size();
    }

    std::vector<uint8_t> file_data(
        body.begin() + static_cast<std::ptrdiff_t>(data_start),
        body.begin() + static_cast<std::ptrdiff_t>(data_end));

    return std::make_pair(filename, std::move(file_data));
}

} // anonymous namespace

// ── MessagesHandler ───────────────────────────────────────────────────────────

MessagesHandler::MessagesHandler(std::shared_ptr<CassandraClient> cass,
                                  std::shared_ptr<PgPool>           pg,
                                  std::shared_ptr<JwtService>       jwt,
                                  std::shared_ptr<RedisClient>      redis,
                                  std::shared_ptr<SessionRegistry>  registry,
                                  std::shared_ptr<SnowflakeGen>     snowflake)
    : cass_(std::move(cass))
    , pg_(std::move(pg))
    , jwt_(std::move(jwt))
    , redis_(std::move(redis))
    , registry_(std::move(registry))
    , snowflake_(std::move(snowflake))
{}

void MessagesHandler::register_routes(HttpServer& http)
{
    auto cass     = cass_;
    auto pg       = pg_;
    auto jwt      = jwt_;
    auto redis    = redis_;
    auto registry = registry_;

    // ── DELETE /messages/{msg_id}?conv_id={conv_id} ───────────────────────────
    http.add_route(http::verb::delete_, "/messages/{msg_id}",
        [cass, pg, jwt, redis, registry](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            uint64_t msg_id = 0;
            {
                auto& s = params.at("msg_id");
                auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), msg_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid msg_id");
            }

            auto conv_id_sv = query_param(std::string_view(req.target()), "conv_id");
            if (conv_id_sv.empty()) co_return make_error(http::status::bad_request, "conv_id required");

            uint64_t conv_id = 0;
            {
                auto [p, ec] = std::from_chars(conv_id_sv.data(), conv_id_sv.data() + conv_id_sv.size(), conv_id);
                if (ec != std::errc{}) co_return make_error(http::status::bad_request, "invalid conv_id");
            }

            uint64_t caller_id = static_cast<uint64_t>(user->uid);

            // Fetch conversation members (try DM first, then group)
            std::vector<uint64_t> members;
            try {
                auto rows = co_await pg->execute(
                    [conv_id](pqxx::connection& conn) {
                        pqxx::nontransaction ntxn(conn);
                        return ntxn.exec_params(
                            "SELECT user_id FROM conv_members WHERE conv_id=$1",
                            static_cast<int64_t>(conv_id));
                    },
                    PgPool::RetryClass::ReadOnly);
                for (const auto& row : rows)
                    members.push_back(static_cast<uint64_t>(row[0].as<int64_t>()));

                if (members.empty()) {
                    rows = co_await pg->execute(
                        [conv_id](pqxx::connection& conn) {
                            pqxx::nontransaction ntxn(conn);
                            return ntxn.exec_params(
                                "SELECT user_id FROM group_members WHERE group_id=$1",
                                static_cast<int64_t>(conv_id));
                        },
                        PgPool::RetryClass::ReadOnly);
                    for (const auto& row : rows)
                        members.push_back(static_cast<uint64_t>(row[0].as<int64_t>()));
                }
            } catch (const std::exception& e) {
                LOG_ERROR("DELETE /messages/{}: member lookup: {}", msg_id, e.what());
                co_return make_error(http::status::internal_server_error, "database error");
            }

            bool is_member = std::find(members.begin(), members.end(), caller_id) != members.end();
            if (!is_member) co_return make_error(http::status::forbidden, "not a member");

            // Delete from Cassandra
            cass->delete_message(conv_id, msg_id);

            // Fan-out DELETE_NOTIFY to all members
            int64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            OutboundMessage del_msg;
            del_msg.conv_id      = conv_id;
            del_msg.msg_id       = msg_id;
            del_msg.sender_id    = caller_id;
            del_msg.recipient_id = 0;
            del_msg.timestamp_ms = ts_ms;
            del_msg.msg_type     = MsgType::DELETE_NOTIFY;
            del_msg.flags        = 0;

            auto delivery_bytes = serialize_delivery(del_msg);

            for (auto member_id : members) {
                auto session = registry->lookup(member_id);
                if (session) {
                    net::post(session->strand(),
                              [session, delivery = delivery_bytes]() mutable {
                                  session->enqueue(std::move(delivery));
                              });
                } else {
                    co_await redis->lpush("offline:" + std::to_string(member_id),
                                         std::span<const uint8_t>(delivery_bytes));
                }
            }

            Response res{http::status::no_content, 11};
            res.prepare_payload();
            co_return res;
        });

    // ── POST /upload ──────────────────────────────────────────────────────────
    http.add_route(http::verb::post, "/upload",
        [jwt](const Request& req, const PathParams&)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            // Extract boundary from Content-Type header
            auto ct = std::string(req[http::field::content_type]);
            std::string boundary;
            {
                auto pos = ct.find("boundary=");
                if (pos == std::string::npos)
                    co_return make_error(http::status::bad_request, "missing boundary");
                boundary = ct.substr(pos + 9);
                // Strip optional quotes
                if (!boundary.empty() && boundary.front() == '"') boundary.erase(0, 1);
                if (!boundary.empty() && boundary.back() == '"')  boundary.pop_back();
            }

            auto parsed = parse_multipart(req.body(), boundary);
            if (!parsed) co_return make_error(http::status::bad_request, "failed to parse multipart");

            auto& [filename, file_data] = *parsed;
            auto ext = extract_extension(filename);

            auto uuid    = generate_uuid();
            auto out_name = uuid + ext;

            try {
                fs::create_directories("uploads");
                std::ofstream ofs("uploads/" + out_name, std::ios::binary);
                if (!ofs) co_return make_error(http::status::internal_server_error, "failed to write file");
                ofs.write(reinterpret_cast<const char*>(file_data.data()),
                          static_cast<std::streamsize>(file_data.size()));
            } catch (const std::exception& e) {
                LOG_ERROR("POST /upload: {}", e.what());
                co_return make_error(http::status::internal_server_error, "file write error");
            }

            nlohmann::json resp;
            resp["url"] = "/files/" + out_name;
            co_return make_json(http::status::ok, resp.dump());
        });

    // ── GET /files/{uuid} ─────────────────────────────────────────────────────
    http.add_route(http::verb::get, "/files/{uuid}",
        [jwt](const Request& req, const PathParams& params)
        -> net::awaitable<Response> {
            auto user = require_auth(std::string(req[http::field::authorization]), *jwt);
            if (!user) co_return make_error(http::status::forbidden, "missing or invalid token");

            auto& uuid = params.at("uuid");

            // Reject path traversal attempts
            if (uuid.find('/') != std::string::npos ||
                uuid.find('\\') != std::string::npos ||
                uuid.find("..") != std::string::npos) {
                co_return make_error(http::status::bad_request, "invalid filename");
            }

            auto path = "uploads/" + uuid;
            std::ifstream ifs(path, std::ios::binary);
            if (!ifs) co_return make_error(http::status::not_found, "file not found");

            std::string content((std::istreambuf_iterator<char>(ifs)),
                                 std::istreambuf_iterator<char>());

            auto ct = content_type_for(uuid);

            Response res{http::status::ok, 11};
            res.set(http::field::content_type, ct);
            res.body() = std::move(content);
            res.prepare_payload();
            co_return res;
        });
}

} // namespace Loomic
