-- ============================================================================
-- Migration 002: Session Management
-- Mô tả: Tạo bảng sessions lưu trữ phiên đăng nhập với mã băm token_hash,
--        hỗ trợ multi-session, thời hạn phiên và khóa ngoại tới bảng users.
-- ============================================================================

PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE TRANSACTION;

CREATE TABLE IF NOT EXISTS sessions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER NOT NULL,
    token_hash TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    expires_at TEXT NOT NULL,
    revoked_at TEXT,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_sessions_token_hash ON sessions(token_hash);
CREATE INDEX IF NOT EXISTS idx_sessions_user_id ON sessions(user_id);

INSERT INTO schema_migrations (version) VALUES ('002_sessions');

COMMIT;
