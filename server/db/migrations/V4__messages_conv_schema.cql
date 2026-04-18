-- Migrate messages table to use conv_id as partition key for conversation history.
DROP TABLE IF EXISTS loomic.messages;
CREATE TABLE loomic.messages (
    conv_id      bigint,
    msg_id       bigint,
    sender_id    bigint,
    recipient_id bigint,
    content      blob,
    msg_type     tinyint,
    ts           timestamp,
    PRIMARY KEY (conv_id, msg_id)
) WITH CLUSTERING ORDER BY (msg_id DESC)
  AND default_time_to_live = 7776000;
