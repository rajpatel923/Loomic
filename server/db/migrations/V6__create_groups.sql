CREATE TABLE groups (
    id         BIGINT        PRIMARY KEY,
    name       TEXT          NOT NULL,
    creator_id BIGINT        NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    created_at TIMESTAMPTZ   NOT NULL DEFAULT NOW()
);
CREATE TABLE group_members (
    group_id   BIGINT  NOT NULL REFERENCES groups(id) ON DELETE CASCADE,
    user_id    BIGINT  NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    role       TEXT    NOT NULL CHECK (role IN ('admin', 'member')),
    joined_at  TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (group_id, user_id)
);
CREATE INDEX group_members_user_idx ON group_members(user_id);
ALTER TABLE users ADD COLUMN bio        TEXT;
ALTER TABLE users ADD COLUMN avatar_url TEXT;
