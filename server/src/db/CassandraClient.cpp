#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/util/Logger.hpp"

#include "chat.pb.h"

#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

// Base64 encoding (minimal, for dead-letter log)
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#if LOOMIC_HAS_CASSANDRA
#include <cassandra.h>
#endif

namespace Loomic {

namespace {

std::string base64_encode(const std::vector<uint8_t>& data)
{
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    b64  = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), static_cast<int>(data.size()));
    BIO_flush(b64);
    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

} // namespace

#if LOOMIC_HAS_CASSANDRA

CassandraClient::CassandraClient(const Config& cfg)
    : keyspace_(cfg.cassandra_keyspace)
{
    // Dead-letter logger
    try {
        dead_letter_log_ = spdlog::basic_logger_mt(
            "dead_letters", "logs/dead_letters.log");
        dead_letter_log_->set_pattern("%v");
        dead_letter_log_->flush_on(spdlog::level::warn);
    } catch (const spdlog::spdlog_ex&) {
        dead_letter_log_ = spdlog::get("dead_letters");
    }

    cluster_ = cass_cluster_new();
    session_ = cass_session_new();

    cass_cluster_set_contact_points(cluster_, cfg.cassandra_contact_points.c_str());
    cass_cluster_set_port(cluster_, cfg.cassandra_port);
    cass_cluster_set_credentials(cluster_,
                                  cfg.cassandra_username.c_str(),
                                  cfg.cassandra_password.c_str());

    if (cfg.cassandra_ssl) {
        ssl_ = cass_ssl_new();
        if (!cfg.cassandra_ca_cert.empty()) {
            std::string ca_pem = cfg.cassandra_ca_cert;
            for (size_t pos = 0; (pos = ca_pem.find("\\n", pos)) != std::string::npos; )
                ca_pem.replace(pos, 2, "\n");

            CassError rc = cass_ssl_add_trusted_cert(ssl_, ca_pem.c_str());
            if (rc != CASS_OK) {
                throw std::runtime_error(
                    std::string("CassandraClient: failed to load CA cert: ") + cass_error_desc(rc));
            }
            cass_ssl_set_verify_flags(ssl_, CASS_SSL_VERIFY_PEER_CERT);
        } else {
            cass_ssl_set_verify_flags(ssl_, CASS_SSL_VERIFY_NONE);
        }
        cass_cluster_set_ssl(cluster_, ssl_);
    }

    CassFuture* connect_future = cass_session_connect(session_, cluster_);
    CassError rc = cass_future_error_code(connect_future);
    cass_future_free(connect_future);

    if (rc != CASS_OK) {
        throw std::runtime_error(
            std::string("CassandraClient: connect failed: ") + cass_error_desc(rc));
    }

    // INSERT statement
    {
        const std::string cql =
            "INSERT INTO " + keyspace_ +
            ".messages (conv_id, msg_id, sender_id, recipient_id, content, msg_type, ts)"
            " VALUES (?, ?, ?, ?, ?, ?, ?)";
        CassFuture* f = cass_session_prepare(session_, cql.c_str());
        rc = cass_future_error_code(f);
        if (rc != CASS_OK) {
            cass_future_free(f);
            throw std::runtime_error(
                std::string("CassandraClient: prepare INSERT failed: ") + cass_error_desc(rc));
        }
        insert_msg_ = cass_future_get_prepared(f);
        cass_future_free(f);
    }

    // SELECT HISTORY statement
    {
        const std::string cql =
            "SELECT conv_id, msg_id, sender_id, recipient_id, content, msg_type, ts"
            " FROM " + keyspace_ +
            ".messages WHERE conv_id=? AND msg_id < ? LIMIT ?";
        CassFuture* f = cass_session_prepare(session_, cql.c_str());
        rc = cass_future_error_code(f);
        if (rc != CASS_OK) {
            cass_future_free(f);
            throw std::runtime_error(
                std::string("CassandraClient: prepare SELECT failed: ") + cass_error_desc(rc));
        }
        select_hist_ = cass_future_get_prepared(f);
        cass_future_free(f);
    }

    // DELETE statement
    {
        const std::string cql =
            "DELETE FROM " + keyspace_ +
            ".messages WHERE conv_id=? AND msg_id=?";
        CassFuture* f = cass_session_prepare(session_, cql.c_str());
        rc = cass_future_error_code(f);
        if (rc != CASS_OK) {
            cass_future_free(f);
            throw std::runtime_error(
                std::string("CassandraClient: prepare DELETE failed: ") + cass_error_desc(rc));
        }
        delete_msg_ = cass_future_get_prepared(f);
        cass_future_free(f);
    }

    LOG_INFO("CassandraClient connected to {}", cfg.cassandra_contact_points);
}

CassandraClient::~CassandraClient()
{
    pool_.stop();
    pool_.join();
    if (insert_msg_)  cass_prepared_free(insert_msg_);
    if (select_hist_) cass_prepared_free(select_hist_);
    if (delete_msg_)  cass_prepared_free(delete_msg_);
    if (session_) {
        CassFuture* close_future = cass_session_close(session_);
        cass_future_free(close_future);
        cass_session_free(session_);
    }
    if (cluster_) cass_cluster_free(cluster_);
    if (ssl_)     cass_ssl_free(ssl_);
}

net::awaitable<void> CassandraClient::store_message_async(
    uint64_t conv_id, uint64_t msg_id, uint64_t sender_id,
    uint64_t recipient_id, std::vector<uint8_t> content,
    int64_t ts_ms, uint8_t msg_type)
{
    co_await net::post(pool_, net::use_awaitable);

    if (!insert_msg_) co_return;

    constexpr int kMaxAttempts = 3;
    const int backoff_ms[] = {100, 400, 1600};

    CassError last_rc = CASS_OK;
    std::string last_err;

    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (attempt > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(backoff_ms[attempt - 1]));
        }

        CassStatement* stmt = cass_prepared_bind(insert_msg_);
        cass_statement_bind_int64(stmt, 0, static_cast<cass_int64_t>(conv_id));
        cass_statement_bind_int64(stmt, 1, static_cast<cass_int64_t>(msg_id));
        cass_statement_bind_int64(stmt, 2, static_cast<cass_int64_t>(sender_id));
        cass_statement_bind_int64(stmt, 3, static_cast<cass_int64_t>(recipient_id));
        cass_statement_bind_bytes(stmt, 4,
            reinterpret_cast<const cass_byte_t*>(content.data()), content.size());
        cass_statement_bind_int8(stmt, 5, static_cast<cass_int8_t>(msg_type));
        cass_statement_bind_int64(stmt, 6, static_cast<cass_int64_t>(ts_ms));

        CassFuture* future = cass_session_execute(session_, stmt);
        cass_statement_free(stmt);

        last_rc = cass_future_error_code(future);
        if (last_rc != CASS_OK) {
            const char* msg; size_t len;
            cass_future_error_message(future, &msg, &len);
            last_err = std::string(msg, len);
            LOG_WARN("CassandraClient: store attempt {}/{} failed for msg_id={}: {}",
                     attempt + 1, kMaxAttempts, msg_id, last_err);
        }
        cass_future_free(future);

        if (last_rc == CASS_OK) co_return;
    }

    // All attempts failed — write to dead letter log
    if (dead_letter_log_) {
        auto payload_b64 = base64_encode(content);
        dead_letter_log_->warn(
            R"({{"ts":{},"msg_id":{},"conv_id":{},"sender_id":{},"recipient_id":{},"msg_type":{},"payload_b64":"{}","error":"{}"}})",
            ts_ms, msg_id, conv_id, sender_id, recipient_id,
            static_cast<int>(msg_type), payload_b64, last_err);
    }
    LOG_ERROR("CassandraClient: msg_id={} permanently failed after {} attempts, written to dead letter log",
              msg_id, kMaxAttempts);
}

net::awaitable<std::vector<loomic::ChatMessage>>
CassandraClient::select_history(uint64_t conv_id, uint64_t before_msg_id, uint32_t limit)
{
    co_await net::post(pool_, net::use_awaitable);

    std::vector<loomic::ChatMessage> results;
    if (!select_hist_) co_return results;

    CassStatement* stmt = cass_prepared_bind(select_hist_);
    cass_statement_bind_int64(stmt, 0, static_cast<cass_int64_t>(conv_id));
    cass_statement_bind_int64(stmt, 1, static_cast<cass_int64_t>(before_msg_id));
    cass_statement_bind_int32(stmt, 2, static_cast<cass_int32_t>(limit));

    CassFuture* future = cass_session_execute(session_, stmt);
    cass_statement_free(stmt);

    CassError rc = cass_future_error_code(future);
    if (rc != CASS_OK) {
        const char* msg; size_t len;
        cass_future_error_message(future, &msg, &len);
        LOG_ERROR("CassandraClient: select_history failed: {}", std::string(msg, len));
        cass_future_free(future);
        co_return results;
    }

    const CassResult* cass_result = cass_future_get_result(future);
    cass_future_free(future);

    CassIterator* iter = cass_iterator_from_result(cass_result);
    while (cass_iterator_next(iter)) {
        const CassRow* row = cass_iterator_get_row(iter);

        cass_int64_t r_conv_id, r_msg_id, r_sender_id, r_recipient_id, r_ts;
        cass_int8_t  r_msg_type;
        const cass_byte_t* r_content; size_t r_content_len;

        cass_value_get_int64(cass_row_get_column(row, 0), &r_conv_id);
        cass_value_get_int64(cass_row_get_column(row, 1), &r_msg_id);
        cass_value_get_int64(cass_row_get_column(row, 2), &r_sender_id);
        cass_value_get_int64(cass_row_get_column(row, 3), &r_recipient_id);
        cass_value_get_bytes(cass_row_get_column(row, 4), &r_content, &r_content_len);
        cass_value_get_int8(cass_row_get_column(row, 5), &r_msg_type);
        cass_value_get_int64(cass_row_get_column(row, 6), &r_ts);

        loomic::ChatMessage msg;
        msg.set_msg_id(static_cast<uint64_t>(r_msg_id));
        msg.set_sender_id(static_cast<uint64_t>(r_sender_id));
        msg.set_recipient_id(static_cast<uint64_t>(r_recipient_id));
        msg.set_content(reinterpret_cast<const char*>(r_content), r_content_len);
        msg.set_timestamp_ms(static_cast<int64_t>(r_ts));
        msg.set_type(static_cast<loomic::MessageType>(r_msg_type));

        results.push_back(std::move(msg));
    }

    cass_iterator_free(iter);
    cass_result_free(cass_result);

    co_return results;
}

void CassandraClient::delete_message(uint64_t conv_id, uint64_t msg_id)
{
    if (!delete_msg_) return;

    CassStatement* stmt = cass_prepared_bind(delete_msg_);
    cass_statement_bind_int64(stmt, 0, static_cast<cass_int64_t>(conv_id));
    cass_statement_bind_int64(stmt, 1, static_cast<cass_int64_t>(msg_id));

    CassFuture* future = cass_session_execute(session_, stmt);
    cass_statement_free(stmt);

    // Fire-and-forget
    cass_future_free(future);
}

#else // !LOOMIC_HAS_CASSANDRA

CassandraClient::CassandraClient(const Config& cfg)
    : keyspace_(cfg.cassandra_keyspace)
{
    (void)cfg;
    LOG_WARN("Cassandra support disabled; messages will not be persisted to Cassandra");
}

CassandraClient::~CassandraClient() = default;

net::awaitable<void> CassandraClient::store_message_async(
    uint64_t conv_id, uint64_t msg_id, uint64_t sender_id,
    uint64_t recipient_id, std::vector<uint8_t> content,
    int64_t ts_ms, uint8_t msg_type)
{
    (void)conv_id; (void)msg_id; (void)sender_id; (void)recipient_id;
    (void)content; (void)ts_ms; (void)msg_type;
    co_return;
}

net::awaitable<std::vector<loomic::ChatMessage>>
CassandraClient::select_history(uint64_t conv_id, uint64_t before_msg_id, uint32_t limit)
{
    (void)conv_id; (void)before_msg_id; (void)limit;
    co_return std::vector<loomic::ChatMessage>{};
}

void CassandraClient::delete_message(uint64_t conv_id, uint64_t msg_id)
{
    (void)conv_id; (void)msg_id;
}

#endif // LOOMIC_HAS_CASSANDRA

} // namespace Loomic
