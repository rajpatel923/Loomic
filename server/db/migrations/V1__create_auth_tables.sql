CREATE TABLE IF NOT EXISTS users (
    id            BIGINT       PRIMARY KEY,
    username      TEXT         NOT NULL UNIQUE,
    email         TEXT         NOT NULL UNIQUE,
    password_hash TEXT         NOT NULL,
    created_at    TIMESTAMPTZ  NOT NULL DEFAULT NOW()
);

CREATE TABLE IF NOT EXISTS refresh_tokens (
    token         TEXT         PRIMARY KEY,
    user_id       BIGINT       NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    expires_at    TIMESTAMPTZ  NOT NULL
);
