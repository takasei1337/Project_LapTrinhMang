-- ============================================================================
-- Migration 001: Classification Schema Foundation
-- Mô tả: Bổ sung 4 bảng danh mục phân cấp (subjects, chapters, lessons, topics)
--        và thêm cột topic_id nullable vào bảng questions hiện tại.
-- ============================================================================

PRAGMA foreign_keys = ON;

BEGIN IMMEDIATE TRANSACTION;

-- 1. Bảng ghi nhận lịch sử migration của hệ thống
CREATE TABLE IF NOT EXISTS schema_migrations (
    version TEXT PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- 2. Bảng Môn học (Subjects)
CREATE TABLE subjects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT NOT NULL UNIQUE CHECK (length(trim(code)) > 0),
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX idx_subjects_code ON subjects(code);

-- 3. Bảng Chương (Chapters)
CREATE TABLE chapters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    subject_id INTEGER NOT NULL,
    order_index INTEGER NOT NULL CHECK (order_index > 0),
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (subject_id) REFERENCES subjects(id) ON DELETE RESTRICT,
    UNIQUE (subject_id, order_index),
    UNIQUE (subject_id, name)
);
CREATE INDEX idx_chapters_subject_id ON chapters(subject_id);

-- 4. Bảng Bài học (Lessons)
CREATE TABLE lessons (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    chapter_id INTEGER NOT NULL,
    order_index INTEGER NOT NULL CHECK (order_index > 0),
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (chapter_id) REFERENCES chapters(id) ON DELETE RESTRICT,
    UNIQUE (chapter_id, order_index),
    UNIQUE (chapter_id, name)
);
CREATE INDEX idx_lessons_chapter_id ON lessons(chapter_id);

-- 5. Bảng Chủ đề kiến thức (Topics)
CREATE TABLE topics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lesson_id INTEGER NOT NULL,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (lesson_id) REFERENCES lessons(id) ON DELETE RESTRICT,
    UNIQUE (lesson_id, name)
);
CREATE INDEX idx_topics_lesson_id ON topics(lesson_id);
CREATE INDEX idx_topics_name ON topics(name COLLATE NOCASE);

-- 6. Bổ sung cột topic_id nullable vào bảng questions (giữ nguyên topic TEXT cũ và toàn bộ dữ liệu)
ALTER TABLE questions ADD COLUMN topic_id INTEGER REFERENCES topics(id) ON DELETE RESTRICT;
CREATE INDEX idx_questions_topic_id ON questions(topic_id);

-- 7. Ghi nhận phiên bản migration đã hoàn thành
INSERT INTO schema_migrations (version) VALUES ('001_classification');

COMMIT;

