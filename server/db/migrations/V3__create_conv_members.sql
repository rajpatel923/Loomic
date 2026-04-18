CREATE TABLE conv_members (
    conv_id   BIGINT NOT NULL,
    user_id   BIGINT NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    joined_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (conv_id, user_id)
);
CREATE INDEX ON conv_members (user_id);
