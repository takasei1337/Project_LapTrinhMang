#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
    } \
} while (0)

static char *read_file_to_string(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rb = fread(buf, 1, (size_t)size, f);
    buf[rb] = '\0';
    fclose(f);
    return buf;
}

static int exec_sql(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        sqlite3_free(err_msg);
        return rc;
    }
    return SQLITE_OK;
}

static int exec_sql_expect_err(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
    }
    return rc;
}

static int table_exists(sqlite3 *db, const char *table)
{
    const char *sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, table, -1, SQLITE_STATIC);
    int exists = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return exists > 0;
}

static int column_exists(sqlite3 *db, const char *table, const char *column)
{
    char sql[256];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int found = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(stmt, 1);
        if (name && strcmp(name, column) == 0) {
            found = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static int count_fk_violations(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int violations = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        violations++;
    }
    sqlite3_finalize(stmt);
    return violations;
}

static int is_migration_applied(sqlite3 *db, const char *version)
{
    if (!table_exists(db, "schema_migrations")) {
        return 0;
    }
    const char *sql = "SELECT COUNT(*) FROM schema_migrations WHERE version = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
    int applied = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        applied = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);
    return applied;
}

/*
 * ============================================================================
 * MIGRATION RUNNER AN TOÀN (SAFE MIGRATION ENGINE)
 * ============================================================================
 * - Kiểm tra lịch sử migration (schema_migrations).
 * - Kiểm tra baseline schema (nếu DB tạo từ schema.sql đã có topic_id, reconcile version thay vì chạy lại DDL).
 * - Bắt buộc kiểm tra autocommit và gọi ROLLBACK ngay khi sqlite3_exec() gặp lỗi.
 * - Đảm bảo autocommit == 1 ở mọi nhánh thoát.
 */
static int run_migration_safe(sqlite3 *db, const char *migration_path, const char *version)
{
    if (db == NULL || migration_path == NULL || version == NULL) {
        return -1;
    }

    /* 1. Đảm bảo bảng schema_migrations tồn tại */
    const char *create_tbl_sql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "    version TEXT PRIMARY KEY,"
        "    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP"
        ");";
    if (exec_sql(db, create_tbl_sql) != SQLITE_OK) {
        return -2;
    }

    /* 2. Nếu đã ghi nhận version này, bỏ qua an toàn */
    if (is_migration_applied(db, version)) {
        return 0;
    }

    /* 3. Kiểm tra trường hợp baseline: DB đã có topic_id và subjects (ví dụ tạo từ schema.sql) */
    if (column_exists(db, "questions", "topic_id") == 1 &&
        table_exists(db, "topics") == 1) {
        /* Đã có schema classification, chỉ cần ghi nhận version để đồng bộ */
        const char *ins_sql = "INSERT OR IGNORE INTO schema_migrations (version) VALUES (?);";
        sqlite3_stmt *stmt = NULL;
        if (sqlite3_prepare_v2(db, ins_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
        return 0;
    }

    /* 4. Đọc file migration SQL */
    char *sql = read_file_to_string(migration_path);
    if (sql == NULL) {
        return -3;
    }

    /* 5. Thực thi script migration */
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    free(sql);

    /* 6. Xử lý lỗi và thu hồi Transaction bắt buộc */
    if (rc != SQLITE_OK) {
        if (err_msg) {
            sqlite3_free(err_msg);
        }
        /* Nếu lỗi xảy ra giữa chừng sau BEGIN, SQLite không tự ROLLBACK. Phải rollback tường minh */
        if (sqlite3_get_autocommit(db) == 0) {
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        }
        return -4;
    }

    /* 7. Xác nhận transaction đã commit và foreign_key_check hợp lệ */
    if (sqlite3_get_autocommit(db) == 0) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
        return -5;
    }

    return 0;
}

/* Old schema SQL without classification tables and without topic_id */
static const char *OLD_SCHEMA_SQL =
    "PRAGMA foreign_keys = ON;\n"
    "CREATE TABLE users (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    username TEXT NOT NULL UNIQUE,\n"
    "    password_hash TEXT NOT NULL,\n"
    "    role TEXT NOT NULL DEFAULT 'USER' CHECK (role IN ('USER', 'ADMIN')),\n"
    "    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
    ");\n"
    "CREATE TABLE questions (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    content TEXT NOT NULL,\n"
    "    topic TEXT NOT NULL,\n"
    "    difficulty TEXT NOT NULL CHECK (difficulty IN ('EASY', 'MEDIUM', 'HARD')),\n"
    "    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
    ");\n"
    "CREATE TABLE answers (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    question_id INTEGER NOT NULL,\n"
    "    content TEXT NOT NULL,\n"
    "    is_correct INTEGER NOT NULL DEFAULT 0 CHECK (is_correct IN (0, 1)),\n"
    "    FOREIGN KEY (question_id) REFERENCES questions(id)\n"
    ");\n"
    "CREATE TABLE rooms (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    owner_id INTEGER NOT NULL,\n"
    "    name TEXT NOT NULL,\n"
    "    question_count INTEGER NOT NULL CHECK (question_count > 0),\n"
    "    duration_minutes INTEGER NOT NULL CHECK (duration_minutes > 0),\n"
    "    start_time TEXT,\n"
    "    end_time TEXT,\n"
    "    status TEXT NOT NULL DEFAULT 'WAITING' CHECK (status IN ('WAITING', 'IN_PROGRESS', 'FINISHED')),\n"
    "    FOREIGN KEY (owner_id) REFERENCES users(id)\n"
    ");\n"
    "CREATE TABLE room_members (\n"
    "    room_id INTEGER NOT NULL,\n"
    "    user_id INTEGER NOT NULL,\n"
    "    joined_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    PRIMARY KEY (room_id, user_id),\n"
    "    FOREIGN KEY (room_id) REFERENCES rooms(id),\n"
    "    FOREIGN KEY (user_id) REFERENCES users(id)\n"
    ");\n"
    "CREATE TABLE exam_attempts (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    room_id INTEGER,\n"
    "    user_id INTEGER NOT NULL,\n"
    "    mode TEXT NOT NULL CHECK (mode IN ('EXAM', 'PRACTICE')),\n"
    "    started_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    submitted_at TEXT,\n"
    "    score REAL,\n"
    "    FOREIGN KEY (room_id) REFERENCES rooms(id),\n"
    "    FOREIGN KEY (user_id) REFERENCES users(id)\n"
    ");\n"
    "CREATE TABLE attempt_answers (\n"
    "    attempt_id INTEGER NOT NULL,\n"
    "    question_id INTEGER NOT NULL,\n"
    "    answer_id INTEGER NOT NULL,\n"
    "    PRIMARY KEY (attempt_id, question_id),\n"
    "    FOREIGN KEY (attempt_id) REFERENCES exam_attempts(id),\n"
    "    FOREIGN KEY (question_id) REFERENCES questions(id),\n"
    "    FOREIGN KEY (answer_id) REFERENCES answers(id)\n"
    ");\n"
    "CREATE TABLE activity_logs (\n"
    "    id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
    "    user_id INTEGER,\n"
    "    action TEXT NOT NULL,\n"
    "    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,\n"
    "    FOREIGN KEY (user_id) REFERENCES users(id)\n"
    ");\n"
    "CREATE TABLE room_questions (\n"
    "    room_id INTEGER NOT NULL,\n"
    "    question_id INTEGER NOT NULL,\n"
    "    position INTEGER NOT NULL CHECK (position > 0),\n"
    "    PRIMARY KEY (room_id, question_id),\n"
    "    UNIQUE (room_id, position),\n"
    "    FOREIGN KEY (room_id) REFERENCES rooms(id),\n"
    "    FOREIGN KEY (question_id) REFERENCES questions(id)\n"
    ");\n"
    "CREATE TABLE practice_questions (\n"
    "    attempt_id INTEGER NOT NULL,\n"
    "    question_id INTEGER NOT NULL,\n"
    "    position INTEGER NOT NULL CHECK (position > 0),\n"
    "    PRIMARY KEY (attempt_id, question_id),\n"
    "    UNIQUE (attempt_id, position),\n"
    "    FOREIGN KEY (attempt_id) REFERENCES exam_attempts(id),\n"
    "    FOREIGN KEY (question_id) REFERENCES questions(id)\n"
    ");\n";

/*
 * ============================================================================
 * NHÓM 1: KIỂM THỬ SCHEMA MỚI TẠO TỪ database/schema.sql
 * ============================================================================
 */
static void test_group1_new_schema(void)
{
    printf("\n=== NHÓM 1: Kiểm thử New Schema từ database/schema.sql ===\n");

    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    TEST_ASSERT(rc == SQLITE_OK && db != NULL, "1a. Mở database in-memory thành công");

    char *schema_sql = read_file_to_string("database/schema.sql");
    TEST_ASSERT(schema_sql != NULL, "1b. Đọc file database/schema.sql thành công");

    rc = exec_sql(db, schema_sql);
    TEST_ASSERT(rc == SQLITE_OK, "1c. Thực thi toàn bộ database/schema.sql thành công");
    free(schema_sql);

    /* 2. Có đủ bốn bảng danh mục + schema_migrations */
    TEST_ASSERT(table_exists(db, "subjects") == 1, "2a. Bảng 'subjects' tồn tại");
    TEST_ASSERT(table_exists(db, "chapters") == 1, "2b. Bảng 'chapters' tồn tại");
    TEST_ASSERT(table_exists(db, "lessons") == 1, "2c. Bảng 'lessons' tồn tại");
    TEST_ASSERT(table_exists(db, "topics") == 1, "2d. Bảng 'topics' tồn tại");
    TEST_ASSERT(table_exists(db, "schema_migrations") == 1, "2e. Bảng 'schema_migrations' tồn tại");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "2f. schema.sql tự động ghi nhận phiên bản '001_classification'");

    /* 3. Có cột questions.topic_id */
    TEST_ASSERT(column_exists(db, "questions", "topic_id") == 1, "3a. Cột 'questions.topic_id' tồn tại");
    TEST_ASSERT(column_exists(db, "questions", "topic") == 1, "3b. Cột 'questions.topic' cũ được giữ nguyên");

    /* PRAGMA foreign_key_check ban đầu */
    TEST_ASSERT(count_fk_violations(db) == 0, "3c. PRAGMA foreign_key_check ban đầu trả về 0 vi phạm");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "3d. Autocommit đang bật (autocommit == 1) sau khi khởi tạo schema");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 2: KIỂM THỬ RÀNG BUỘC FK, UNIQUE, CHECK TRÊN CÂY DANH MỤC
 * ============================================================================
 */
static void test_group2_constraints(void)
{
    printf("\n=== NHÓM 2: Kiểm thử Constraints (FK, UNIQUE, CHECK) ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    char *schema_sql = read_file_to_string("database/schema.sql");
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* 4a. Chèn dữ liệu danh mục hợp lệ */
    int rc = exec_sql(db, "INSERT INTO subjects (id, code, name, description) VALUES (1, 'IT4060', 'Lập trình mạng', 'Môn học LTM');");
    TEST_ASSERT(rc == SQLITE_OK, "4a1. Thêm Subject hợp lệ thành công");

    rc = exec_sql(db, "INSERT INTO chapters (id, subject_id, order_index, name) VALUES (1, 1, 1, 'Chương 1: Tổng quan');");
    TEST_ASSERT(rc == SQLITE_OK, "4a2. Thêm Chapter hợp lệ thành công");

    rc = exec_sql(db, "INSERT INTO lessons (id, chapter_id, order_index, name) VALUES (1, 1, 1, 'Bài 1: Kiến trúc Client-Server');");
    TEST_ASSERT(rc == SQLITE_OK, "4a3. Thêm Lesson hợp lệ thành công");

    rc = exec_sql(db, "INSERT INTO topics (id, lesson_id, name) VALUES (1, 1, 'Socket TCP');");
    TEST_ASSERT(rc == SQLITE_OK, "4a4. Thêm Topic hợp lệ thành công");

    /* 4b. UNIQUE constraints */
    rc = exec_sql_expect_err(db, "INSERT INTO subjects (code, name) VALUES ('IT4060', 'Trùng mã môn');");
    TEST_ASSERT(rc != SQLITE_OK, "4b1. Chặn trùng subjects.code (UNIQUE)");

    rc = exec_sql_expect_err(db, "INSERT INTO chapters (subject_id, order_index, name) VALUES (1, 1, 'Chương trùng thứ tự');");
    TEST_ASSERT(rc != SQLITE_OK, "4b2. Chặn trùng chapters order_index trong cùng môn (UNIQUE)");

    rc = exec_sql_expect_err(db, "INSERT INTO chapters (subject_id, order_index, name) VALUES (1, 2, 'Chương 1: Tổng quan');");
    TEST_ASSERT(rc != SQLITE_OK, "4b3. Chặn trùng chapters name trong cùng môn (UNIQUE)");

    rc = exec_sql_expect_err(db, "INSERT INTO lessons (chapter_id, order_index, name) VALUES (1, 1, 'Bài trùng thứ tự');");
    TEST_ASSERT(rc != SQLITE_OK, "4b4. Chặn trùng lessons order_index trong cùng chương (UNIQUE)");

    rc = exec_sql_expect_err(db, "INSERT INTO lessons (chapter_id, order_index, name) VALUES (1, 2, 'Bài 1: Kiến trúc Client-Server');");
    TEST_ASSERT(rc != SQLITE_OK, "4b5. Chặn trùng lessons name trong cùng chương (UNIQUE)");

    rc = exec_sql_expect_err(db, "INSERT INTO topics (lesson_id, name) VALUES (1, 'Socket TCP');");
    TEST_ASSERT(rc != SQLITE_OK, "4b6. Chặn trùng topics name trong cùng bài (UNIQUE)");

    /* 4c. CHECK constraints */
    rc = exec_sql_expect_err(db, "INSERT INTO subjects (code, name) VALUES ('', 'Tên hợp lệ');");
    TEST_ASSERT(rc != SQLITE_OK, "4c1. Chặn subjects.code rỗng (CHECK length(trim) > 0)");

    rc = exec_sql_expect_err(db, "INSERT INTO subjects (code, name) VALUES ('   ', 'Tên hợp lệ');");
    TEST_ASSERT(rc != SQLITE_OK, "4c2. Chặn subjects.code toàn whitespace (CHECK length(trim) > 0)");

    rc = exec_sql_expect_err(db, "INSERT INTO chapters (subject_id, order_index, name) VALUES (1, 0, 'Thứ tự không hợp lệ');");
    TEST_ASSERT(rc != SQLITE_OK, "4c3. Chặn chapters order_index <= 0 (CHECK order_index > 0)");

    /* 4d. FK constraints cho danh mục */
    rc = exec_sql_expect_err(db, "INSERT INTO chapters (subject_id, order_index, name) VALUES (999, 5, 'Subject không tồn tại');");
    TEST_ASSERT(rc != SQLITE_OK, "4d1. Chặn thêm chapter trỏ tới subject_id không tồn tại (FK)");

    rc = exec_sql_expect_err(db, "INSERT INTO lessons (chapter_id, order_index, name) VALUES (999, 5, 'Chapter không tồn tại');");
    TEST_ASSERT(rc != SQLITE_OK, "4d2. Chặn thêm lesson trỏ tới chapter_id không tồn tại (FK)");

    rc = exec_sql_expect_err(db, "INSERT INTO topics (lesson_id, name) VALUES (999, 'Lesson không tồn tại');");
    TEST_ASSERT(rc != SQLITE_OK, "4d3. Chặn thêm topic trỏ tới lesson_id không tồn tại (FK)");

    /* 4e. FK questions.topic_id */
    rc = exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty, topic_id) VALUES (100, 'Câu hỏi TCP?', 'Network', 'EASY', 1);");
    TEST_ASSERT(rc == SQLITE_OK, "4e1. Thêm question với topic_id hợp lệ thành công");

    rc = exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty, topic_id) VALUES (101, 'Câu hỏi NULL topic_id?', 'Network', 'EASY', NULL);");
    TEST_ASSERT(rc == SQLITE_OK, "4e2. Thêm question với topic_id = NULL thành công");

    rc = exec_sql_expect_err(db, "INSERT INTO questions (id, content, topic, difficulty, topic_id) VALUES (102, 'Câu hỏi topic_id giả?', 'Network', 'EASY', 9999);");
    TEST_ASSERT(rc != SQLITE_OK, "4e3. Chặn thêm question với topic_id không tồn tại (FK questions.topic_id)");

    TEST_ASSERT(count_fk_violations(db) == 0, "4e4. Toàn bộ cơ sở dữ liệu đạt 0 lỗi foreign_key_check");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 3: KIỂM THỬ ON DELETE RESTRICT (KHÔNG CHO PHÉP XÓA KHI CÒN DỮ LIỆU CON)
 * ============================================================================
 */
static void test_group3_on_delete_restrict(void)
{
    printf("\n=== NHÓM 3: Kiểm thử ON DELETE RESTRICT ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    char *schema_sql = read_file_to_string("database/schema.sql");
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* Thiết lập cây phân cấp mẫu */
    exec_sql(db, "INSERT INTO subjects (id, code, name) VALUES (1, 'MATH', 'Toán');");
    exec_sql(db, "INSERT INTO chapters (id, subject_id, order_index, name) VALUES (1, 1, 1, 'Giải tích');");
    exec_sql(db, "INSERT INTO lessons (id, chapter_id, order_index, name) VALUES (1, 1, 1, 'Đạo hàm');");
    exec_sql(db, "INSERT INTO topics (id, lesson_id, name) VALUES (1, 1, 'Quy tắc tính đạo hàm');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty, topic_id) VALUES (1, 'Đạo hàm x^2?', 'Math', 'EASY', 1);");

    /* 5a. Thử xóa topic khi có câu hỏi tham chiếu */
    int rc = exec_sql_expect_err(db, "DELETE FROM topics WHERE id = 1;");
    TEST_ASSERT(rc != SQLITE_OK, "5a. Chặn xóa topic khi có question tham chiếu (RESTRICT)");

    /* 5b. Thử xóa lesson khi có topic tham chiếu */
    rc = exec_sql_expect_err(db, "DELETE FROM lessons WHERE id = 1;");
    TEST_ASSERT(rc != SQLITE_OK, "5b. Chặn xóa lesson khi có topic tham chiếu (RESTRICT)");

    /* 5c. Thử xóa chapter khi có lesson tham chiếu */
    rc = exec_sql_expect_err(db, "DELETE FROM chapters WHERE id = 1;");
    TEST_ASSERT(rc != SQLITE_OK, "5c. Chặn xóa chapter khi có lesson tham chiếu (RESTRICT)");

    /* 5d. Thử xóa subject khi có chapter tham chiếu */
    rc = exec_sql_expect_err(db, "DELETE FROM subjects WHERE id = 1;");
    TEST_ASSERT(rc != SQLITE_OK, "5d. Chặn xóa subject khi có chapter tham chiếu (RESTRICT)");

    /* 5e. Sau khi gỡ liên kết hoặc xóa từ dưới lên, xóa thành công */
    rc = exec_sql(db, "UPDATE questions SET topic_id = NULL WHERE id = 1;");
    TEST_ASSERT(rc == SQLITE_OK, "5e1. Cập nhật question.topic_id = NULL thành công");

    rc = exec_sql(db, "DELETE FROM topics WHERE id = 1;");
    TEST_ASSERT(rc == SQLITE_OK, "5e2. Xóa topic thành công sau khi không còn question tham chiếu");

    rc = exec_sql(db, "DELETE FROM lessons WHERE id = 1;");
    TEST_ASSERT(rc == SQLITE_OK, "5e3. Xóa lesson thành công sau khi không còn topic");

    rc = exec_sql(db, "DELETE FROM chapters WHERE id = 1;");
    TEST_ASSERT(rc == SQLITE_OK, "5e4. Xóa chapter thành công sau khi không còn lesson");

    rc = exec_sql(db, "DELETE FROM subjects WHERE id = 1;");
    TEST_ASSERT(rc == SQLITE_OK, "5e5. Xóa subject thành công sau khi không còn chapter");

    TEST_ASSERT(count_fk_violations(db) == 0, "5e6. Sau khi xóa, PRAGMA foreign_key_check trả về 0 vi phạm");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 4: KIỂM THỬ MIGRATION LẦN ĐẦU VÀ BẢO TOÀN DỮ LIỆU
 * ============================================================================
 */
static void test_group4_migration_first_run(void)
{
    printf("\n=== NHÓM 4: Kiểm thử Migration Lần Đầu (001_classification.sql) ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);

    /* 1. Thiết lập Database theo Schema cũ */
    int rc = exec_sql(db, OLD_SCHEMA_SQL);
    TEST_ASSERT(rc == SQLITE_OK, "6a. Khởi tạo database theo Schema cũ thành công");
    TEST_ASSERT(table_exists(db, "subjects") == 0, "6b. Ban đầu bảng 'subjects' chưa tồn tại");
    TEST_ASSERT(column_exists(db, "questions", "topic_id") == 0, "6c. Ban đầu cột 'questions.topic_id' chưa tồn tại");

    /* 2. Chèn dữ liệu cũ */
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (1, 'alice', 'hash1');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (10, 'Câu 10: TCP là gì?', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (20, 'Câu 20: Tiến trình là gì?', 'OS', 'MEDIUM');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (30, 'Câu 30: Khóa chính là gì?', 'Database', 'HARD');");

    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (101, 10, 'Giao thức hướng nối', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (102, 10, 'Giao thức không nối', 0);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (201, 20, 'Chương trình đang thực thi', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (301, 30, 'Định danh duy nhất', 1);");

    exec_sql(db, "INSERT INTO exam_attempts (id, user_id, mode) VALUES (500, 1, 'PRACTICE');");
    exec_sql(db, "INSERT INTO practice_questions (attempt_id, question_id, position) VALUES (500, 10, 1);");
    exec_sql(db, "INSERT INTO practice_questions (attempt_id, question_id, position) VALUES (500, 20, 2);");
    exec_sql(db, "INSERT INTO attempt_answers (attempt_id, question_id, answer_id) VALUES (500, 10, 101);");

    /* 3. Chạy migration an toàn lần đầu */
    rc = run_migration_safe(db, "database/migrations/001_classification.sql", "001_classification");
    TEST_ASSERT(rc == 0, "6e. Chạy run_migration_safe() lần đầu thành công");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "6e_ac. Autocommit = 1 sau khi migration hoàn tất");

    /* 4. Kiểm tra cấu trúc bảng sau migration */
    TEST_ASSERT(table_exists(db, "subjects") == 1, "6f. Bảng 'subjects' đã được tạo");
    TEST_ASSERT(table_exists(db, "chapters") == 1, "6g. Bảng 'chapters' đã được tạo");
    TEST_ASSERT(table_exists(db, "lessons") == 1, "6h. Bảng 'lessons' đã được tạo");
    TEST_ASSERT(table_exists(db, "topics") == 1, "6i. Bảng 'topics' đã được tạo");
    TEST_ASSERT(table_exists(db, "schema_migrations") == 1, "6j. Bảng 'schema_migrations' tồn tại");
    TEST_ASSERT(column_exists(db, "questions", "topic_id") == 1, "6k. Cột 'questions.topic_id' đã được bổ sung");

    /* 5. ID câu hỏi và đáp án không thay đổi */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT id, content, topic, difficulty, topic_id FROM questions ORDER BY id ASC;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "7a. Truy vấn dữ liệu questions sau migration thành công");

    int q_ids[3] = {0};
    char q_topics[3][32] = {{0}};
    int q_topic_id_null[3] = {0};
    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count < 3) {
            q_ids[count] = sqlite3_column_int(stmt, 0);
            const char *top = (const char *)sqlite3_column_text(stmt, 2);
            if (top) strncpy(q_topics[count], top, sizeof(q_topics[count]) - 1);
            q_topic_id_null[count] = (sqlite3_column_type(stmt, 4) == SQLITE_NULL);
            count++;
        }
    }
    sqlite3_finalize(stmt);

    TEST_ASSERT(count == 3, "7b. Số lượng câu hỏi cũ bảo toàn nguyên vẹn (3 câu)");
    TEST_ASSERT(q_ids[0] == 10 && q_ids[1] == 20 && q_ids[2] == 30, "7c. ID câu hỏi cũ giữ nguyên (10, 20, 30)");

    /* 6. questions.topic cũ được giữ nguyên */
    TEST_ASSERT(strcmp(q_topics[0], "Network") == 0 &&
                strcmp(q_topics[1], "OS") == 0 &&
                strcmp(q_topics[2], "Database") == 0,
                "8. Giá trị questions.topic cũ ('Network', 'OS', 'Database') bảo toàn 100%");

    /* 7. topic_id của câu hỏi cũ vẫn NULL */
    TEST_ASSERT(q_topic_id_null[0] && q_topic_id_null[1] && q_topic_id_null[2],
                "9. Cột topic_id của toàn bộ câu hỏi cũ vẫn là NULL (không tự gán giả định)");

    /* Kiểm tra ID đáp án và liên kết attempt_answers bảo toàn */
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM answers WHERE id IN (101, 102, 201, 301);", -1, &stmt, NULL);
    int ans_count = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        ans_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(ans_count == 4, "7d. ID và số lượng đáp án cũ bảo toàn nguyên vẹn");

    /* 8. PRAGMA foreign_key_check trả về 0 vi phạm */
    TEST_ASSERT(count_fk_violations(db) == 0, "11. PRAGMA foreign_key_check sau migration trả về 0 vi phạm");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "10a. Phiên bản '001_classification' đã được lưu trong schema_migrations");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 5: KIỂM THỬ CHẠY LẠI MIGRATION LẦN HAI VÀ TRANSACTION SAFETY
 * ============================================================================
 */
static void test_group5_rerun_and_transaction_safety(void)
{
    printf("\n=== NHÓM 5: Kiểm thử Chạy lại Migration Lần Hai & Transaction Safety ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, OLD_SCHEMA_SQL);
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (1, 'Q1', 'Network', 'EASY');");

    /* Chạy migration lần 1 thành công */
    int rc = run_migration_safe(db, "database/migrations/001_classification.sql", "001_classification");
    TEST_ASSERT(rc == 0, "R1. Migration lần 1 thành công");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "R2. Autocommit = 1 sau migration lần 1");

    /*
     * THÍ NGHIỆM 1 (Tái hiện lỗi Task 06.3.1):
     * Khi gọi sqlite3_exec() thô trực tiếp với file SQL migration lần 2:
     * - Script gặp lỗi tại 'CREATE TABLE subjects' vì bảng đã tồn tại.
     * - SQLite dừng thực thi và KHÔNG chạy tiếp COMMIT;.
     * - SQLite KHÔNG tự rollback transaction! sqlite3_get_autocommit(db) trở thành 0!
     */
    char *raw_sql = read_file_to_string("database/migrations/001_classification.sql");
    char *err_msg = NULL;
    int raw_rc = sqlite3_exec(db, raw_sql, NULL, NULL, &err_msg);
    free(raw_sql);

    TEST_ASSERT(raw_rc != SQLITE_OK, "R3. sqlite3_exec() thô lần 2 thất bại như dự kiến (bảng đã tồn tại)");

    /* Kiểm tra thực tế trạng thái autocommit sau khi chạy thô gặp lỗi */
    int ac_after_err = sqlite3_get_autocommit(db);
    TEST_ASSERT(ac_after_err == 0, "R4. XÁC NHẬN: SQLite để hở transaction (autocommit == 0) khi gặp lỗi DDL giữa chừng");

    if (err_msg) {
        sqlite3_free(err_msg);
    }

    /* Nếu không rollback, cố gắng bắt đầu transaction tiếp theo sẽ bị lỗi */
    rc = exec_sql_expect_err(db, "BEGIN IMMEDIATE TRANSACTION;");
    TEST_ASSERT(rc != SQLITE_OK, "R5. Cố gắng BEGIN transaction mới khi transaction cũ chưa đóng sẽ thất bại");

    /* Thu hồi tường minh transaction bị hở */
    rc = exec_sql(db, "ROLLBACK;");
    TEST_ASSERT(rc == SQLITE_OK, "R6. Gọi ROLLBACK; tường minh thành công");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "R7. Autocommit phục hồi về 1 sau khi ROLLBACK");

    /* Dữ liệu câu hỏi cũ không bị ảnh hưởng sau khi rollback */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM questions WHERE id = 1;", -1, &stmt, NULL);
    int q_cnt = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        q_cnt = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(q_cnt == 1, "R8. Dữ liệu câu hỏi cũ bảo toàn nguyên vẹn sau lỗi và rollback");

    /*
     * THÍ NGHIỆM 2: Sử dụng run_migration_safe() lần 2
     * Runner phát hiện migration đã chạy qua schema_migrations, bỏ qua an toàn
     */
    rc = run_migration_safe(db, "database/migrations/001_classification.sql", "001_classification");
    TEST_ASSERT(rc == 0, "R9. run_migration_safe() lần 2 thành công (skip an toàn, không lỗi)");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "R10. Autocommit duy trì bằng 1 sau khi runner bỏ qua an toàn");
    TEST_ASSERT(count_fk_violations(db) == 0, "R11. PRAGMA foreign_key_check đạt 0 vi phạm");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 6: KIỂM THỬ LỖI GIỮA MIGRATION VÀ BẢO ĐẢM KHÔNG ĐỂ HỞ TRANSACTION
 * ============================================================================
 */
static void test_group6_faulty_migration_rollback(void)
{
    printf("\n=== NHÓM 6: Kiểm thử Lỗi Giữa Migration & Đảm bảo Không để hở Transaction ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, OLD_SCHEMA_SQL);
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (1, 'Old Q', 'Old Topic', 'EASY');");

    /* Tạo một file migration lỗi giả lập: tạo subjects thành công, sau đó gặp câu lệnh cú pháp sai */
    const char *faulty_file_path = "/tmp/test_faulty_migration.sql";
    FILE *f = fopen(faulty_file_path, "wb");
    const char *faulty_sql =
        "PRAGMA foreign_keys = ON;\n"
        "BEGIN IMMEDIATE TRANSACTION;\n"
        "CREATE TABLE subjects (id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE, name TEXT NOT NULL);\n"
        "SYNTAX_ERROR_TRIGGER_FAILURE;\n"
        "INSERT INTO schema_migrations (version) VALUES ('999_faulty');\n"
        "COMMIT;\n";
    fwrite(faulty_sql, 1, strlen(faulty_sql), f);
    fclose(f);

    /* Chạy qua safe runner */
    int rc = run_migration_safe(db, faulty_file_path, "999_faulty");
    TEST_ASSERT(rc != 0, "F1. run_migration_safe() báo lỗi khi script migration có lỗi cú pháp giữa chừng");

    /* Kiểm tra quan trọng: Transaction không được bỏ mở */
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "F2. Autocommit được đảm bảo phục hồi về 1 (không để hở transaction)");

    /* Kiểm tra: Bảng subjects không được lưu dở dang (đã rollback hoàn toàn) */
    TEST_ASSERT(table_exists(db, "subjects") == 0, "F3. Bảng 'subjects' không tồn tại (đã rollback triệt để)");

    /* Kiểm tra: Không đánh dấu version thành công trong schema_migrations khi DDL chưa hoàn tất */
    TEST_ASSERT(is_migration_applied(db, "999_faulty") == 0,
                "F4. Không đánh dấu thành công version 999_faulty trong schema_migrations");

    /* Kiểm tra: Dữ liệu cũ còn nguyên */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT content, topic FROM questions WHERE id = 1;", -1, &stmt, NULL);
    int row_ok = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(stmt, 0);
        const char *t = (const char *)sqlite3_column_text(stmt, 1);
        if (c && strcmp(c, "Old Q") == 0 && t && strcmp(t, "Old Topic") == 0) {
            row_ok = 1;
        }
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(row_ok == 1, "F5. Dữ liệu cũ trong questions được bảo toàn nguyên vẹn sau khi rollback");

    remove(faulty_file_path);
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 7: KIỂM THỬ DATABASE TẠO TỪ schema.sql KHÔNG BỊ ÁP DỤNG LẠI MIGRATION
 * ============================================================================
 */
static void test_group7_fresh_schema_reconciliation(void)
{
    printf("\n=== NHÓM 7: Database tạo từ schema.sql & Đồng bộ Baseline Migration ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);

    /* 1. Tạo database từ database/schema.sql */
    char *schema_sql = read_file_to_string("database/schema.sql");
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* Kiểm tra database mới đã có sẵn 001_classification qua schema.sql */
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "S1. schema.sql tự động ghi nhận phiên bản '001_classification'");

    /*
     * 2. Tình huống đặc biệt (Công việc 5):
     * Giả lập trường hợp DB được tạo bằng schema.sql đã có cột topic_id,
     * nhưng bảng schema_migrations vì lý do nào đó CHƯA CÓ bản ghi '001_classification'
     */
    exec_sql(db, "DELETE FROM schema_migrations WHERE version = '001_classification';");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 0,
                "S2. Giả lập xóa bản ghi '001_classification' khỏi schema_migrations");
    TEST_ASSERT(column_exists(db, "questions", "topic_id") == 1,
                "S3. Xác nhận cột 'questions.topic_id' và các bảng danh mục đã tồn tại trong DB");

    /*
     * Gọi run_migration_safe():
     * Runner phải nhận diện baseline: các bảng và cột của 001 đã tồn tại sẵn,
     * nên KHÔNG chạy lại file 001_classification.sql (tránh lỗi duplicate table/column),
     * mà tự động ghi nhận '001_classification' vào schema_migrations và trả về thành công!
     */
    int rc = run_migration_safe(db, "database/migrations/001_classification.sql", "001_classification");
    TEST_ASSERT(rc == 0, "S4. run_migration_safe() điều hòa baseline thành công, không chạy lại DDL xung đột");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "S5. Autocommit duy trì bằng 1 sau khi điều hòa");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "S6. schema_migrations đã được phục hồi ghi nhận '001_classification'");
    TEST_ASSERT(count_fk_violations(db) == 0, "S7. PRAGMA foreign_key_check đạt 0 vi phạm");

    sqlite3_close(db);
}

int main(void)
{
    printf("=================================================================\n");
    printf("  BỘ KIỂM THỬ: CLASSIFICATION SCHEMA & MIGRATION SAFETY (06.3.1)\n");
    printf("=================================================================\n");

    test_group1_new_schema();
    test_group2_constraints();
    test_group3_on_delete_restrict();
    test_group4_migration_first_run();
    test_group5_rerun_and_transaction_safety();
    test_group6_faulty_migration_rollback();
    test_group7_fresh_schema_reconciliation();

    printf("\n=================================================================\n");
    printf("  KẾT QUẢ KIỂM THỬ: %d / %d PASS\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");

    return (g_tests_passed == g_tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
