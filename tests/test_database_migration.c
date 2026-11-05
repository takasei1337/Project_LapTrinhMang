#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>

#include "database.h"

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
    "    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP\n"
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
 * NHÓM 1: VALIDATION THAM SỐ VÀ ĐƯỜNG DẪN THƯ MỤC
 * ============================================================================
 */
static void test_group1_param_validation(void)
{
    printf("\n=== NHÓM 1: Validation Tham số & Đường dẫn Thư mục ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);

    int rc = database_apply_migrations(NULL, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_PARAM, "1a. db == NULL -> DATABASE_MIGRATION_ERR_PARAM");

    rc = database_apply_migrations(db, NULL);
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_PARAM, "1b. migrations_dir == NULL -> DATABASE_MIGRATION_ERR_PARAM");

    rc = database_apply_migrations(db, "");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_PARAM, "1c. migrations_dir rỗng -> DATABASE_MIGRATION_ERR_PARAM");

    rc = database_apply_migrations(db, "/tmp/dir_that_does_not_exist_at_all_12345");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_DIR, "1d. Thư mục không tồn tại -> DATABASE_MIGRATION_ERR_DIR");

    /* Truyền file thay vì thư mục */
    rc = database_apply_migrations(db, "Makefile");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_DIR, "1e. Đường dẫn là file thay vì thư mục -> DATABASE_MIGRATION_ERR_DIR");

    /* Thư mục rỗng không chứa file .sql */
    const char *empty_dir = "/tmp/test_empty_mig_dir_g1";
    mkdir(empty_dir, 0755);
    rc = database_apply_migrations(db, empty_dir);
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_DIR, "1f. Thư mục rỗng không có file SQL -> DATABASE_MIGRATION_ERR_DIR");
    rmdir(empty_dir);

    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "1g. Autocommit luôn duy trì bằng 1 sau validation thất bại");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 2: BẢO VỆ TRANSACTION CỦA CALLER (REQUIREMENT 6)
 * ============================================================================
 */
static void test_group2_caller_transaction_protection(void)
{
    printf("\n=== NHÓM 2: Bảo vệ Transaction của Caller ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, "CREATE TABLE dummy (val INT);");

    /* Caller mở transaction */
    exec_sql(db, "BEGIN IMMEDIATE TRANSACTION;");
    exec_sql(db, "INSERT INTO dummy VALUES (42);");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "2a. Caller mở transaction thành công (autocommit == 0)");

    /* Runner bị từ chối và KHÔNG được tự ý rollback */
    int rc = database_apply_migrations(db, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_TRANSACTION,
                "2b. Từ chối chạy khi caller đang mở transaction (DATABASE_MIGRATION_ERR_TRANSACTION)");

    TEST_ASSERT(sqlite3_get_autocommit(db) == 0,
                "2c. KHÔNG tự ý rollback transaction của caller (autocommit vẫn == 0)");

    /* Caller commit bình thường */
    rc = exec_sql(db, "COMMIT;");
    TEST_ASSERT(rc == SQLITE_OK, "2d. Caller hoàn tất COMMIT giao dịch thành công");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "2e. Autocommit trở về 1 sau khi caller COMMIT");

    /* Dữ liệu của caller vẫn được bảo toàn */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT val FROM dummy;", -1, &stmt, NULL);
    int val = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        val = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(val == 42, "2f. Dữ liệu của caller được bảo toàn nguyên vẹn");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 3: MIGRATION DATABASE CŨ CHƯA CÓ CLASSIFICATION (LẦN ĐẦU & LẦN HAI)
 * ============================================================================
 */
static void test_group3_legacy_migration_and_rerun(void)
{
    printf("\n=== NHÓM 3: Migration Database cũ chưa có Classification ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, OLD_SCHEMA_SQL);

    /* Chèn dữ liệu cũ */
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (1, 'u1', 'h1');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (10, 'Q10 Net', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (20, 'Q20 OS', 'OS', 'MEDIUM');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (101, 10, 'Ans 101', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (201, 20, 'Ans 201', 1);");

    /* 1. Chạy migration lần đầu */
    int rc = database_apply_migrations(db, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_SUCCESS, "3a. database_apply_migrations() lần đầu thành công");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "3b. Autocommit = 1 sau migration lần đầu");

    /* Kiểm tra các bảng phân loại và cột topic_id */
    TEST_ASSERT(table_exists(db, "subjects") == 1, "3c. Bảng 'subjects' tồn tại");
    TEST_ASSERT(table_exists(db, "chapters") == 1, "3d. Bảng 'chapters' tồn tại");
    TEST_ASSERT(table_exists(db, "lessons") == 1, "3e. Bảng 'lessons' tồn tại");
    TEST_ASSERT(table_exists(db, "topics") == 1, "3f. Bảng 'topics' tồn tại");
    TEST_ASSERT(column_exists(db, "questions", "topic_id") == 1, "3g. Cột 'topic_id' đã được bổ sung");

    /* Kiểm tra bảo toàn dữ liệu cũ */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT id, content, topic, topic_id FROM questions ORDER BY id ASC;", -1, &stmt, NULL);
    int q10_ok = 0, q20_ok = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int qid = sqlite3_column_int(stmt, 0);
        const char *top = (const char *)sqlite3_column_text(stmt, 2);
        int is_null = (sqlite3_column_type(stmt, 3) == SQLITE_NULL);
        if (qid == 10 && top && strcmp(top, "Network") == 0 && is_null) q10_ok = 1;
        if (qid == 20 && top && strcmp(top, "OS") == 0 && is_null) q20_ok = 1;
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(q10_ok && q20_ok, "3h. Dữ liệu câu hỏi cũ (id, topic, topic_id == NULL) bảo toàn 100%");

    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "3i. Version '001_classification' được ghi nhận trong schema_migrations");
    TEST_ASSERT(count_fk_violations(db) == 0, "3j. PRAGMA foreign_key_check đạt 0 vi phạm");

    /* 2. Chạy migration lần hai (Idempotency) */
    rc = database_apply_migrations(db, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_SUCCESS, "3k. Chạy lại migration lần 2 thành công (skip an toàn)");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "3l. Autocommit duy trì bằng 1 sau lần chạy thứ 2");
    TEST_ASSERT(count_fk_violations(db) == 0, "3m. Foreign key check vẫn đạt 0 vi phạm");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 4: DATABASE MỚI TẠO TỪ schema.sql VÀ ĐIỀU HÒA BASELINE
 * ============================================================================
 */
static void test_group4_fresh_schema_and_reconciliation(void)
{
    printf("\n=== NHÓM 4: Database Mới từ schema.sql & Điều hòa Baseline ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);

    char *schema_sql = read_file_to_string("database/schema.sql");
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* 1. Database mới từ schema.sql đã có sẵn version 001_classification */
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "4a. schema.sql tự động ghi nhận '001_classification'");

    int rc = database_apply_migrations(db, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_SUCCESS, "4b. database_apply_migrations() trên DB mới thành công (skip)");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "4c. Autocommit = 1");

    /* 2. Điều hòa baseline: DB có đủ schema nhưng thiếu bản ghi log (Requirement 7 & 8) */
    exec_sql(db, "DELETE FROM schema_migrations WHERE version = '001_classification';");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 0,
                "4d. Xóa bản ghi '001_classification' trong schema_migrations");

    rc = database_apply_migrations(db, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_SUCCESS, "4e. Runner tự động điều hòa baseline thành công");
    TEST_ASSERT(is_migration_applied(db, "001_classification") == 1,
                "4f. Version '001_classification' được phục hồi ghi nhận vào schema_migrations");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "4g. Autocommit = 1");
    TEST_ASSERT(count_fk_violations(db) == 0, "4h. PRAGMA foreign_key_check đạt 0 vi phạm");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 5: CHẶN COI SCHEMA CHƯA ĐẦY ĐỦ LÀ ĐÃ MIGRATE (REQUIREMENT 7)
 * ============================================================================
 */
static void test_group5_incomplete_schema_protection(void)
{
    printf("\n=== NHÓM 5: Bảo vệ chống nhận nhầm Schema chưa đầy đủ ===\n");

    /* Trường hợp A: DB chỉ có bảng subjects, thiếu chapters/lessons/topics/topic_id */
    sqlite3 *db_a = NULL;
    sqlite3_open(":memory:", &db_a);
    exec_sql(db_a, OLD_SCHEMA_SQL);
    exec_sql(db_a, "CREATE TABLE subjects (id INTEGER PRIMARY KEY, code TEXT UNIQUE, name TEXT);");

    /* Gọi runner */
    int rc = database_apply_migrations(db_a, "database/migrations");
    TEST_ASSERT(rc != DATABASE_MIGRATION_SUCCESS,
                "5a. Không tự nhận nhầm schema chưa đầy đủ là đã migrate (báo lỗi xung đột bảng)");
    TEST_ASSERT(is_migration_applied(db_a, "001_classification") == 0,
                "5b. Không tự động ghi nhận 001_classification khi schema bị khuyết");
    TEST_ASSERT(sqlite3_get_autocommit(db_a) == 1,
                "5c. Đảm bảo transaction được rollback sạch sẽ (autocommit == 1)");
    sqlite3_close(db_a);

    /* Trường hợp B: DB chỉ có cột topic_id trong questions, thiếu các bảng danh mục */
    sqlite3 *db_b = NULL;
    sqlite3_open(":memory:", &db_b);
    exec_sql(db_b, OLD_SCHEMA_SQL);
    exec_sql(db_b, "ALTER TABLE questions ADD COLUMN topic_id INTEGER;");

    rc = database_apply_migrations(db_b, "database/migrations");
    TEST_ASSERT(rc != DATABASE_MIGRATION_SUCCESS,
                "5d. Từ chối coi schema là đã migrate khi chỉ có cột topic_id đơn lẻ");
    TEST_ASSERT(is_migration_applied(db_b, "001_classification") == 0,
                "5e. Không đánh dấu thành công version 001_classification");
    TEST_ASSERT(sqlite3_get_autocommit(db_b) == 1,
                "5f. Autocommit phục hồi về 1 sau khi rollback");
    sqlite3_close(db_b);

    /* Trường hợp C: DB có đủ tên 4 bảng nhưng bảng chapters bị sai cấu trúc (thiếu subject_id) */
    sqlite3 *db_c = NULL;
    sqlite3_open(":memory:", &db_c);
    exec_sql(db_c, OLD_SCHEMA_SQL);
    exec_sql(db_c, "CREATE TABLE subjects (id INT, code TEXT, name TEXT);");
    exec_sql(db_c, "CREATE TABLE chapters (id INT, name TEXT);"); /* THIẾU subject_id và order_index */
    exec_sql(db_c, "CREATE TABLE lessons (id INT, chapter_id INT, order_index INT, name TEXT);");
    exec_sql(db_c, "CREATE TABLE topics (id INT, lesson_id INT, name TEXT);");
    exec_sql(db_c, "ALTER TABLE questions ADD COLUMN topic_id INTEGER;");

    rc = database_apply_migrations(db_c, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_INCOMPATIBLE,
                "5g. Từ chối coi schema là đã migrate khi bảng có tên đúng nhưng thiếu cột khóa ngoại (subject_id)");
    TEST_ASSERT(is_migration_applied(db_c, "001_classification") == 0,
                "5h. Không tự động đánh dấu 001_classification khi cấu trúc cột sai");
    TEST_ASSERT(sqlite3_get_autocommit(db_c) == 1,
                "5i. Autocommit = 1 sau lỗi cấu trúc cột");
    sqlite3_close(db_c);

    /* Trường hợp D: DB có đủ tên 4 bảng và tên cột FK, nhưng KHÔNG có ràng buộc FOREIGN KEY */
    sqlite3 *db_d = NULL;
    sqlite3_open(":memory:", &db_d);
    exec_sql(db_d, OLD_SCHEMA_SQL);
    exec_sql(db_d, "CREATE TABLE subjects (id INTEGER PRIMARY KEY, code TEXT NOT NULL UNIQUE, name TEXT NOT NULL);");
    exec_sql(db_d, "CREATE TABLE chapters (id INTEGER PRIMARY KEY, subject_id INTEGER NOT NULL, order_index INTEGER NOT NULL, name TEXT NOT NULL);");
    exec_sql(db_d, "CREATE TABLE lessons (id INTEGER PRIMARY KEY, chapter_id INTEGER NOT NULL, order_index INTEGER NOT NULL, name TEXT NOT NULL);");
    exec_sql(db_d, "CREATE TABLE topics (id INTEGER PRIMARY KEY, lesson_id INTEGER NOT NULL, name TEXT NOT NULL);");
    exec_sql(db_d, "ALTER TABLE questions ADD COLUMN topic_id INTEGER;");

    rc = database_apply_migrations(db_d, "database/migrations");
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_INCOMPATIBLE,
                "5j. Cột FK tồn tại nhưng thiếu ràng buộc FOREIGN KEY -> DATABASE_MIGRATION_ERR_INCOMPATIBLE");
    TEST_ASSERT(is_migration_applied(db_d, "001_classification") == 0,
                "5k. Không ghi nhận 001_classification khi thiếu ràng buộc FOREIGN KEY thực tế");
    TEST_ASSERT(sqlite3_get_autocommit(db_d) == 1,
                "5l. Autocommit = 1 sau khi từ chối schema thiếu FK");
    sqlite3_close(db_d);
}

/*
 * ============================================================================
 * NHÓM 6: MIGRATION LỖI GIỮA CHỪNG & ĐẢM BẢO KHÔNG HỞ TRANSACTION
 * ============================================================================
 */
static void test_group6_faulty_migration_handling(void)
{
    printf("\n=== NHÓM 6: Xử lý Migration Lỗi Giữa Chừng & Transaction Safety ===\n");

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, OLD_SCHEMA_SQL);
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (1, 'Q_Orig', 'Net', 'EASY');");

    /* Tạo thư mục tạm và file migration lỗi */
    const char *tmp_dir = "/tmp/test_faulty_mig_dir";
    mkdir(tmp_dir, 0755);

    char faulty_file[256];
    snprintf(faulty_file, sizeof(faulty_file), "%s/002_faulty.sql", tmp_dir);
    FILE *f = fopen(faulty_file, "wb");
    const char *bad_sql =
        "PRAGMA foreign_keys = ON;\n"
        "BEGIN IMMEDIATE TRANSACTION;\n"
        "CREATE TABLE test_tbl (id INT);\n"
        "THIS_IS_A_SYNTAX_ERROR_CRASH;\n"
        "INSERT INTO schema_migrations (version) VALUES ('002_faulty');\n"
        "COMMIT;\n";
    fwrite(bad_sql, 1, strlen(bad_sql), f);
    fclose(f);

    /* Chạy migration lỗi */
    int rc = database_apply_migrations(db, tmp_dir);
    TEST_ASSERT(rc == DATABASE_MIGRATION_ERR_FAILED,
                "6a. database_apply_migrations() trả về DATABASE_MIGRATION_ERR_FAILED");

    /* Kiểm tra transaction safety (Requirement 5) */
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1,
                "6b. Autocommit = 1 sau lỗi (không để hở transaction)");

    /* Kiểm tra bảng tạo dở dang đã bị xóa bởi rollback */
    TEST_ASSERT(table_exists(db, "test_tbl") == 0,
                "6c. Bảng 'test_tbl' không tồn tại (đã rollback triệt để)");

    /* Không ghi nhận version lỗi */
    TEST_ASSERT(is_migration_applied(db, "002_faulty") == 0,
                "6d. Không ghi nhận version lỗi vào schema_migrations");

    /* Dữ liệu cũ còn nguyên */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT content FROM questions WHERE id = 1;", -1, &stmt, NULL);
    int data_ok = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        const char *c = (const char *)sqlite3_column_text(stmt, 0);
        if (c && strcmp(c, "Q_Orig") == 0) data_ok = 1;
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(data_ok == 1, "6e. Dữ liệu cũ trong questions bảo toàn nguyên vẹn sau lỗi");

    unlink(faulty_file);
    rmdir(tmp_dir);
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 7: TÍCH HỢP database_open_with_migrations() VÀ BIẾN MÔI TRƯỜNG
 * ============================================================================
 */
static void test_group7_database_open_integration(void)
{
    printf("\n=== NHÓM 7: Tích hợp database_open_with_migrations() & Env Var ===\n");

    const char *db_file = "/tmp/test_db_open_mig.db";
    unlink(db_file);

    /* 1. Mở với thư mục migration không hợp lệ -> Trả về NULL */
    sqlite3 *db_bad = database_open_with_migrations(db_file, "/tmp/dir_does_not_exist_xyz");
    TEST_ASSERT(db_bad == NULL, "7a. database_open_with_migrations() thất bại khi migrations_dir sai");

    /* 2. Mở với tham số NULL -> Trả về NULL */
    db_bad = database_open_with_migrations(db_file, NULL);
    TEST_ASSERT(db_bad == NULL, "7b. database_open_with_migrations() thất bại khi migrations_dir == NULL");

    /* 3. Mở với thư mục rỗng -> Trả về NULL */
    const char *empty_mig_dir = "/tmp/test_empty_mig_dir_open";
    mkdir(empty_mig_dir, 0755);
    sqlite3 *db_empty = database_open_with_migrations(db_file, empty_mig_dir);
    TEST_ASSERT(db_empty == NULL, "7c. database_open_with_migrations() trả về NULL khi thư mục migration rỗng");
    rmdir(empty_mig_dir);

    /* 4. Mở và migrate thành công trên DB cũ có sẵn bảng questions */
    sqlite3 *db_setup = NULL;
    sqlite3_open(db_file, &db_setup);
    exec_sql(db_setup, OLD_SCHEMA_SQL);
    sqlite3_close(db_setup);

    sqlite3 *db_good = database_open_with_migrations(db_file, "database/migrations");
    TEST_ASSERT(db_good != NULL, "7d. database_open_with_migrations() thành công với 'database/migrations'");
    if (db_good) {
        TEST_ASSERT(is_migration_applied(db_good, "001_classification") == 1,
                    "7e. DB tự động được migrate lên 001_classification");
        TEST_ASSERT(count_fk_violations(db_good) == 0, "7f. Foreign key check đạt 0 vi phạm");
        database_close(db_good);
    }

    /* 5. Mở lại DB đã migrate -> Thành công và không lỗi */
    sqlite3 *db_reopen = database_open_with_migrations(db_file, "database/migrations");
    TEST_ASSERT(db_reopen != NULL, "7g. Mở lại DB đã migrate thành công");
    if (db_reopen) {
        database_close(db_reopen);
    }

    /* 6. Tự động migrate qua biến môi trường MIGRATIONS_DIR */
    const char *db_env = "/tmp/test_db_env.db";
    unlink(db_env);

    sqlite3 *db_env_setup = NULL;
    sqlite3_open(db_env, &db_env_setup);
    exec_sql(db_env_setup, OLD_SCHEMA_SQL);
    sqlite3_close(db_env_setup);

    setenv("MIGRATIONS_DIR", "database/migrations", 1);
    sqlite3 *db_from_env = database_open(db_env);
    TEST_ASSERT(db_from_env != NULL, "7h. database_open() tự động migrate thành công qua biến MIGRATIONS_DIR");
    if (db_from_env) {
        TEST_ASSERT(is_migration_applied(db_from_env, "001_classification") == 1,
                    "7i. DB được migrate lên 001_classification qua env");
        database_close(db_from_env);
    }

    /* Biến môi trường trỏ tới thư mục sai -> database_open trả về NULL (không cho server chạy) */
    setenv("MIGRATIONS_DIR", "/tmp/bad_dir_xyz", 1);
    sqlite3 *db_env_fail = database_open(db_env);
    TEST_ASSERT(db_env_fail == NULL,
                "7j. database_open() trả về NULL khi MIGRATIONS_DIR không hợp lệ");

    unsetenv("MIGRATIONS_DIR");

    /* 7. database_open() khi KHÔNG có MIGRATIONS_DIR: hành vi tương thích ngược, CHỈ mở DB và bật foreign keys */
    const char *db_no_env = "/tmp/test_db_no_env.db";
    unlink(db_no_env);

    sqlite3 *db_ne_setup = NULL;
    sqlite3_open(db_no_env, &db_ne_setup);
    exec_sql(db_ne_setup, OLD_SCHEMA_SQL);
    sqlite3_close(db_ne_setup);

    /* Mở database_open() không set biến môi trường */
    sqlite3 *db_ne = database_open(db_no_env);
    TEST_ASSERT(db_ne != NULL,
                "7k. database_open() mở thành công khi không set MIGRATIONS_DIR (backward compatible)");
    if (db_ne) {
        /* Kiểm tra PRAGMA foreign_keys = ON */
        sqlite3_stmt *fk_stmt = NULL;
        int fk_enabled = 0;
        if (sqlite3_prepare_v2(db_ne, "PRAGMA foreign_keys;", -1, &fk_stmt, NULL) == SQLITE_OK) {
            if (sqlite3_step(fk_stmt) == SQLITE_ROW) {
                fk_enabled = sqlite3_column_int(fk_stmt, 0);
            }
            sqlite3_finalize(fk_stmt);
        }
        TEST_ASSERT(fk_enabled == 1, "7l. PRAGMA foreign_keys = ON được kích hoạt");

        /* Kiểm tra tuyệt đối KHÔNG tự động migrate: các bảng phân loại KHÔNG được tạo */
        TEST_ASSERT(table_exists(db_ne, "subjects") == 0,
                    "7m. Bảng 'subjects' KHÔNG được tạo tự động khi không set MIGRATIONS_DIR");
        TEST_ASSERT(table_exists(db_ne, "topics") == 0,
                    "7n. Bảng 'topics' KHÔNG được tạo tự động");
        TEST_ASSERT(column_exists(db_ne, "questions", "topic_id") == 0,
                    "7o. Cột 'topic_id' KHÔNG được tự động thêm vào");
        TEST_ASSERT(is_migration_applied(db_ne, "001_classification") == 0,
                    "7p. 001_classification CHƯA được áp dụng (bảo tồn nguyên trạng DB cũ)");
        database_close(db_ne);
    }
    unlink(db_no_env);

    unlink(db_file);
    unlink(db_env);
}

int main(void)
{
    printf("=================================================================\n");
    printf("  BỘ KIỂM THỬ: PRODUCTION MIGRATION RUNNER (TASK 06.3.2)\n");
    printf("=================================================================\n");

    test_group1_param_validation();
    test_group2_caller_transaction_protection();
    test_group3_legacy_migration_and_rerun();
    test_group4_fresh_schema_and_reconciliation();
    test_group5_incomplete_schema_protection();
    test_group6_faulty_migration_handling();
    test_group7_database_open_integration();

    printf("\n=================================================================\n");
    printf("  KẾT QUẢ KIỂM THỬ: %d / %d PASS\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");

    return (g_tests_passed == g_tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
