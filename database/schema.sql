PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS schema_migrations (
    version TEXT PRIMARY KEY,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password_hash TEXT NOT NULL,
    role TEXT NOT NULL DEFAULT 'USER'
        CHECK (role IN ('USER', 'ADMIN')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS subjects (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT NOT NULL UNIQUE CHECK (length(trim(code)) > 0),
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_subjects_code ON subjects(code);

CREATE TABLE IF NOT EXISTS chapters (
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
CREATE INDEX IF NOT EXISTS idx_chapters_subject_id ON chapters(subject_id);

CREATE TABLE IF NOT EXISTS lessons (
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
CREATE INDEX IF NOT EXISTS idx_lessons_chapter_id ON lessons(chapter_id);

CREATE TABLE IF NOT EXISTS topics (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    lesson_id INTEGER NOT NULL,
    name TEXT NOT NULL CHECK (length(trim(name)) > 0),
    description TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (lesson_id) REFERENCES lessons(id) ON DELETE RESTRICT,
    UNIQUE (lesson_id, name)
);
CREATE INDEX IF NOT EXISTS idx_topics_lesson_id ON topics(lesson_id);
CREATE INDEX IF NOT EXISTS idx_topics_name ON topics(name COLLATE NOCASE);

CREATE TABLE IF NOT EXISTS questions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    content TEXT NOT NULL,
    topic TEXT NOT NULL,
    difficulty TEXT NOT NULL
        CHECK (difficulty IN ('EASY', 'MEDIUM', 'HARD')),
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    topic_id INTEGER,
    FOREIGN KEY (topic_id) REFERENCES topics(id) ON DELETE RESTRICT
);
CREATE INDEX IF NOT EXISTS idx_questions_topic_id ON questions(topic_id);
CREATE INDEX IF NOT EXISTS idx_questions_difficulty ON questions(difficulty);

CREATE TABLE IF NOT EXISTS answers (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    question_id INTEGER NOT NULL,
    content TEXT NOT NULL,
    is_correct INTEGER NOT NULL DEFAULT 0
        CHECK (is_correct IN (0, 1)),
    FOREIGN KEY (question_id) REFERENCES questions(id)
);

CREATE TABLE IF NOT EXISTS rooms (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    owner_id INTEGER NOT NULL,
    name TEXT NOT NULL,
    question_count INTEGER NOT NULL CHECK (question_count > 0),
    duration_minutes INTEGER NOT NULL CHECK (duration_minutes > 0),
    start_time TEXT,
    end_time TEXT,
    status TEXT NOT NULL DEFAULT 'WAITING'
        CHECK (status IN ('WAITING', 'IN_PROGRESS', 'FINISHED')),
    FOREIGN KEY (owner_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS room_members (
    room_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    joined_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (room_id, user_id),
    FOREIGN KEY (room_id) REFERENCES rooms(id),
    FOREIGN KEY (user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS exam_attempts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    room_id INTEGER,
    user_id INTEGER NOT NULL,
    mode TEXT NOT NULL CHECK (mode IN ('EXAM', 'PRACTICE')),
    started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    submitted_at TEXT,
    score REAL,
    FOREIGN KEY (room_id) REFERENCES rooms(id),
    FOREIGN KEY (user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS attempt_answers (
    attempt_id INTEGER NOT NULL,
    question_id INTEGER NOT NULL,
    answer_id INTEGER NOT NULL,
    PRIMARY KEY (attempt_id, question_id),
    FOREIGN KEY (attempt_id) REFERENCES exam_attempts(id),
    FOREIGN KEY (question_id) REFERENCES questions(id),
    FOREIGN KEY (answer_id) REFERENCES answers(id)
);

CREATE TABLE IF NOT EXISTS activity_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    user_id INTEGER,
    action TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS room_questions (
    room_id INTEGER NOT NULL,
    question_id INTEGER NOT NULL,
    position INTEGER NOT NULL CHECK (position > 0),

    PRIMARY KEY (room_id, question_id),
    UNIQUE (room_id, position),

    FOREIGN KEY (room_id) REFERENCES rooms(id),
    FOREIGN KEY (question_id) REFERENCES questions(id)
);

CREATE TABLE IF NOT EXISTS practice_questions (
    attempt_id INTEGER NOT NULL,
    question_id INTEGER NOT NULL,
    position INTEGER NOT NULL CHECK (position > 0),

    PRIMARY KEY (attempt_id, question_id),
    UNIQUE (attempt_id, position),

    FOREIGN KEY (attempt_id) REFERENCES exam_attempts(id),
    FOREIGN KEY (question_id) REFERENCES questions(id)
);

-- Ghi nhận phiên bản migration nền tảng có sẵn trong schema hiện tại
INSERT OR IGNORE INTO schema_migrations (version) VALUES ('001_classification');
