-- Phase 6: Real-Time UX + Conversation Improvements
-- Adds: last_activity_at / last_msg_preview on convs + groups,
--       last_seen_at on users, message_receipts table, device_tokens table.

ALTER TABLE conversations
    ADD COLUMN IF NOT EXISTS last_activity_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    ADD COLUMN IF NOT EXISTS last_msg_preview  TEXT;
CREATE INDEX IF NOT EXISTS conversations_last_activity_idx ON conversations(last_activity_at DESC);

ALTER TABLE groups
    ADD COLUMN IF NOT EXISTS last_activity_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    ADD COLUMN IF NOT EXISTS last_msg_preview  TEXT,
    ADD COLUMN IF NOT EXISTS avatar_url        TEXT;
CREATE INDEX IF NOT EXISTS groups_last_activity_idx ON groups(last_activity_at DESC);

ALTER TABLE users
    ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMPTZ;

-- status: 1=delivered, 2=read
CREATE TABLE IF NOT EXISTS message_receipts (
    conv_id    BIGINT    NOT NULL,
    msg_id     BIGINT    NOT NULL,
    user_id    BIGINT    NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    status     SMALLINT  NOT NULL CHECK (status IN (1,2)),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (conv_id, msg_id, user_id)
);
CREATE INDEX IF NOT EXISTS message_receipts_lookup_idx ON message_receipts(conv_id, msg_id);

CREATE TABLE IF NOT EXISTS device_tokens (
    user_id    BIGINT    NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    token      TEXT      NOT NULL,
    platform   TEXT      NOT NULL CHECK (platform IN ('ios','android','web')),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (user_id, token)
);
