#pragma once

#include <cstdint>
#include <span>

#include "LoomicServer/util/Config.hpp"

// Forward-declare the opaque DataStax driver handle types to avoid pulling in
// the full cassandra.h in every translation unit that includes this header.
typedef struct CassCluster_   CassCluster;
typedef struct CassSession_   CassSession;
typedef struct CassSsl_       CassSsl;
typedef struct CassPrepared_  CassPrepared;

namespace Loomic {

/// Fire-and-forget Cassandra writer using the DataStax C++ driver.
/// Connects to Azure Cosmos for Apache Cassandra on construction and prepares
/// the INSERT statement once. store_message() is non-blocking; errors are
/// logged asynchronously via a driver callback.
class CassandraClient {
public:
    explicit CassandraClient(const Config& cfg);
    ~CassandraClient();

    void store_message(uint64_t recipient_id, uint64_t msg_id,
                       uint64_t sender_id, std::span<const uint8_t> content,
                       int64_t timestamp_ms, uint8_t msg_type);

private:
    CassCluster*        cluster_    = nullptr;
    CassSession*        session_    = nullptr;
    CassSsl*            ssl_        = nullptr;
    const CassPrepared* insert_msg_ = nullptr;
};

} // namespace Loomic
