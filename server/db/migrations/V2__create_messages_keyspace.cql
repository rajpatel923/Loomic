-- Run against Azure Cosmos for Apache Cassandra (port 10350).
-- Azure Cosmos manages replication; the RF here is advisory only.

CREATE KEYSPACE IF NOT EXISTS loomic
  WITH replication = {'class': 'NetworkTopologyStrategy', 'datacenter1': 1};

CREATE TABLE IF NOT EXISTS loomic.messages (
    recipient_id bigint,
    msg_id       bigint,
    sender_id    bigint,
    content      blob,
    timestamp_ms bigint,
    msg_type     tinyint,
    PRIMARY KEY (recipient_id, msg_id)
) WITH CLUSTERING ORDER BY (msg_id DESC);
