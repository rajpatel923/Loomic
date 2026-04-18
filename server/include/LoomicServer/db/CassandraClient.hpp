#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

#include "LoomicServer/util/Config.hpp"

typedef struct CassCluster_   CassCluster;
typedef struct CassSession_   CassSession;
typedef struct CassSsl_       CassSsl;
typedef struct CassPrepared_  CassPrepared;

namespace spdlog { class logger; }

// Forward-declare protobuf type
namespace loomic { class ChatMessage; }

namespace Loomic {

namespace net = boost::asio;

class CassandraClient {
public:
    explicit CassandraClient(const Config& cfg);
    ~CassandraClient();

    // Async write-behind with retry + dead letter on permanent failure.
    // Call with net::co_spawn(..., net::detached) — does not need co_awaiting.
    net::awaitable<void> store_message_async(
        uint64_t conv_id, uint64_t msg_id, uint64_t sender_id,
        uint64_t recipient_id, std::vector<uint8_t> content,
        int64_t ts_ms, uint8_t msg_type);

    // Fetch up to `limit` messages in conv_id where msg_id < before_msg_id.
    // Returns newest-first (Cassandra CLUSTERING ORDER BY msg_id DESC).
    net::awaitable<std::vector<loomic::ChatMessage>>
        select_history(uint64_t conv_id, uint64_t before_msg_id, uint32_t limit);

    void delete_message(uint64_t conv_id, uint64_t msg_id);

private:
    CassCluster*        cluster_     = nullptr;
    CassSession*        session_     = nullptr;
    CassSsl*            ssl_         = nullptr;
    const CassPrepared* insert_msg_  = nullptr;
    const CassPrepared* select_hist_ = nullptr;
    const CassPrepared* delete_msg_  = nullptr;

    std::shared_ptr<spdlog::logger> dead_letter_log_;
    net::thread_pool                pool_{2};

    std::string keyspace_;
};

} // namespace Loomic
