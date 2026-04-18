-- V5: Create conversations table and add FK from conv_members
-- Run this migration before deploying the Phase 3 server build.

CREATE TABLE conversations (
    id         BIGINT      PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Populate conversations from any existing conv_members rows so the FK
-- is satisfiable.  The id column was previously a synthetic min(a,b) value
-- that may not have a corresponding row; seed it with a zero created_at.
INSERT INTO conversations (id, created_at)
SELECT DISTINCT conv_id, NOW()
FROM conv_members
ON CONFLICT DO NOTHING;

ALTER TABLE conv_members
    ADD CONSTRAINT conv_members_conv_id_fk
    FOREIGN KEY (conv_id) REFERENCES conversations(id) ON DELETE CASCADE;
