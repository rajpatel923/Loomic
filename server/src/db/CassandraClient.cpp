#include "LoomicServer/db/CassandraClient.hpp"
#include "LoomicServer/util/Logger.hpp"

#include <stdexcept>
#include <string>

#if LOOMIC_HAS_CASSANDRA
#include <cassandra.h>
#endif

namespace Loomic {

#if LOOMIC_HAS_CASSANDRA

static void on_store_result(CassFuture* future, void* /*data*/)
{
    if (cass_future_error_code(future) != CASS_OK) {
        const char* msg;
        size_t len;
        cass_future_error_message(future, &msg, &len);
        LOG_ERROR("CassandraClient: store_message failed: {}", std::string(msg, len));
    }
}

CassandraClient::CassandraClient(const Config& cfg)
{
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
            // Unescape \n literals that may appear when the cert is stored in a .env file
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
            // No CA cert provided — skip peer verification (dev/test only)
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

    const std::string cql =
        "INSERT INTO " + cfg.cassandra_keyspace +
        ".messages (recipient_id, msg_id, sender_id, content, timestamp_ms, msg_type)"
        " VALUES (?, ?, ?, ?, ?, ?)";

    CassFuture* prep_future = cass_session_prepare(session_, cql.c_str());
    rc = cass_future_error_code(prep_future);
    if (rc != CASS_OK) {
        cass_future_free(prep_future);
        throw std::runtime_error(
            std::string("CassandraClient: prepare failed: ") + cass_error_desc(rc));
    }
    insert_msg_ = cass_future_get_prepared(prep_future);
    cass_future_free(prep_future);

    LOG_INFO("CassandraClient connected to {}", cfg.cassandra_contact_points);
}

CassandraClient::~CassandraClient()
{
    if (insert_msg_) cass_prepared_free(insert_msg_);
    if (session_) {
        CassFuture* close_future = cass_session_close(session_);
        cass_future_free(close_future);
        cass_session_free(session_);
    }
    if (cluster_) cass_cluster_free(cluster_);
    if (ssl_)     cass_ssl_free(ssl_);
}

void CassandraClient::store_message(uint64_t recipient_id, uint64_t msg_id,
                                    uint64_t sender_id, std::span<const uint8_t> content,
                                    int64_t timestamp_ms, uint8_t msg_type)
{
    if (!insert_msg_) return;

    CassStatement* stmt = cass_prepared_bind(insert_msg_);

    cass_statement_bind_int64(stmt, 0, static_cast<cass_int64_t>(recipient_id));
    cass_statement_bind_int64(stmt, 1, static_cast<cass_int64_t>(msg_id));
    cass_statement_bind_int64(stmt, 2, static_cast<cass_int64_t>(sender_id));
    cass_statement_bind_bytes(stmt, 3,
        reinterpret_cast<const cass_byte_t*>(content.data()), content.size());
    cass_statement_bind_int64(stmt, 4, static_cast<cass_int64_t>(timestamp_ms));
    cass_statement_bind_int8(stmt,  5, static_cast<cass_int8_t>(msg_type));

    CassFuture* future = cass_session_execute(session_, stmt);
    cass_statement_free(stmt);

    // Fire-and-forget: callback logs any error; future freed inside callback
    // via the driver's own internal mechanism once the callback returns.
    cass_future_set_callback(future, on_store_result, nullptr);
    cass_future_free(future);
}

#else

CassandraClient::CassandraClient(const Config& cfg)
{
    (void)cfg;
    LOG_WARN("Cassandra support disabled; messages will not be persisted to Cassandra");
}

CassandraClient::~CassandraClient() = default;

void CassandraClient::store_message(uint64_t recipient_id, uint64_t msg_id,
                                    uint64_t sender_id, std::span<const uint8_t> content,
                                    int64_t timestamp_ms, uint8_t msg_type)
{
    (void)recipient_id;
    (void)msg_id;
    (void)sender_id;
    (void)content;
    (void)timestamp_ms;
    (void)msg_type;
}

#endif

} // namespace Loomic
