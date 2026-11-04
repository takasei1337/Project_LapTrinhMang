#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <assert.h>

#include "logger.h"

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
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static sqlite3 *setup_test_db(void)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open in-memory database: %s\n", sqlite3_errmsg(db));
        if (db) sqlite3_close(db);
        return NULL;
    }

    char *err_msg = NULL;
    rc = sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to enable foreign keys: %s\n", err_msg ? err_msg : "Unknown");
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return NULL;
    }

    char *schema = read_file_to_string("database/schema.sql");
    if (!schema) {
        fprintf(stderr, "Failed to read database/schema.sql\n");
        sqlite3_close(db);
        return NULL;
    }

    rc = sqlite3_exec(db, schema, NULL, NULL, &err_msg);
    free(schema);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to apply schema: %s\n", err_msg ? err_msg : "Unknown");
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return NULL;
    }

    /* Tao nguoi dung mau de test user_id = 1 */
    const char *insert_user = "INSERT INTO users (username, password_hash, role) "
                              "VALUES ('alice', 'secret_hash', 'USER');";
    rc = sqlite3_exec(db, insert_user, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Failed to insert sample user: %s\n", err_msg ? err_msg : "Unknown");
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return NULL;
    }

    return db;
}

static void test_log_valid_event_with_user(sqlite3 *db)
{
    printf("\nTest 1: Ghi su kien hop le co user_id\n");
    int rc = logger_log(db, 1, "AUTH_LOGIN_SUCCESS");
    TEST_ASSERT(rc == 0, "logger_log tra ve 0 khi ghi thanh cong voi user hop le");

    /* Kiem tra du lieu thuc te trong bang activity_logs */
    const char *query = "SELECT user_id, action, created_at FROM activity_logs WHERE action = 'AUTH_LOGIN_SUCCESS';";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT thanh cong");

    if (rc == SQLITE_OK && stmt != NULL) {
        rc = sqlite3_step(stmt);
        TEST_ASSERT(rc == SQLITE_ROW, "Tim thay ban ghi vua ghi");

        if (rc == SQLITE_ROW) {
            int user_id = sqlite3_column_int(stmt, 0);
            const unsigned char *action = sqlite3_column_text(stmt, 1);
            const unsigned char *created_at = sqlite3_column_text(stmt, 2);

            TEST_ASSERT(user_id == 1, "user_id trong CSDL dung voi gia tri da ghi (1)");
            TEST_ASSERT(action != NULL && strcmp((const char *)action, "AUTH_LOGIN_SUCCESS") == 0,
                        "action trong CSDL dung voi gia tri da ghi");
            TEST_ASSERT(created_at != NULL && strlen((const char *)created_at) > 0,
                        "created_at duoc tu dong tao boi DEFAULT CURRENT_TIMESTAMP");
        }

        sqlite3_finalize(stmt);
    }
}

static void test_log_valid_event_without_user(sqlite3 *db)
{
    printf("\nTest 2: Ghi su kien he thong (user_id <= 0 -> NULL trong DB)\n");
    int rc = logger_log(db, 0, "SYSTEM_STARTUP");
    TEST_ASSERT(rc == 0, "logger_log tra ve 0 voi user_id = 0");

    rc = logger_log(db, -1, "SYSTEM_SHUTDOWN");
    TEST_ASSERT(rc == 0, "logger_log tra ve 0 voi user_id = -1");

    /* Kiem tra user_id thuc su la NULL trong CSDL */
    const char *query = "SELECT user_id, action FROM activity_logs WHERE action = 'SYSTEM_STARTUP';";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT thanh cong");

    if (rc == SQLITE_OK && stmt != NULL) {
        rc = sqlite3_step(stmt);
        TEST_ASSERT(rc == SQLITE_ROW, "Tim thay ban ghi SYSTEM_STARTUP");

        if (rc == SQLITE_ROW) {
            TEST_ASSERT(sqlite3_column_type(stmt, 0) == SQLITE_NULL,
                        "user_id trong CSDL thuc su la NULL khi user_id truyen vao la 0");
        }

        sqlite3_finalize(stmt);
    }
}

static void test_invalid_parameters(sqlite3 *db)
{
    printf("\nTest 3: Xu ly tham so khong hop le\n");
    int rc = logger_log(NULL, 1, "SOME_ACTION");
    TEST_ASSERT(rc == -1, "logger_log tra ve -1 khi db == NULL");

    rc = logger_log(db, 1, NULL);
    TEST_ASSERT(rc == -1, "logger_log tra ve -1 khi action == NULL");

    rc = logger_log(db, 1, "");
    TEST_ASSERT(rc == -1, "logger_log tra ve -1 khi action la chuoi rong \"\"");
}

static void test_foreign_key_violation(sqlite3 *db)
{
    printf("\nTest 4: Xu ly vi pham rang buoc khoa ngoai (foreign key constraint)\n");
    /* user_id = 9999 khong ton tai trong bang users */
    int rc = logger_log(db, 9999, "INVALID_FK_ACTION");
    TEST_ASSERT(rc == -1, "logger_log tra ve -1 khi user_id khong ton tai (vi pham FK)");

    /* Kiem tra ban ghi khong duoc chen vao CSDL */
    const char *query = "SELECT COUNT(*) FROM activity_logs WHERE action = 'INVALID_FK_ACTION';";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT thanh cong");

    if (rc == SQLITE_OK && stmt != NULL) {
        rc = sqlite3_step(stmt);
        TEST_ASSERT(rc == SQLITE_ROW, "Doc ket qua COUNT(*)");

        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            TEST_ASSERT(count == 0, "Khong co ban ghi nao duoc chen khi vi pham rang buoc");
        }

        sqlite3_finalize(stmt);
    }
}

static void test_alias_logger_log_activity(sqlite3 *db)
{
    printf("\nTest 5: Kiem tra ham alias logger_log_activity\n");
    int rc = logger_log_activity(db, 1, "ALIAS_TEST_ACTION");
    TEST_ASSERT(rc == 0, "logger_log_activity hoat dong tuong duong logger_log");

    const char *query = "SELECT COUNT(*) FROM activity_logs WHERE action = 'ALIAS_TEST_ACTION';";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT thanh cong");

    if (rc == SQLITE_OK && stmt != NULL) {
        rc = sqlite3_step(stmt);
        TEST_ASSERT(rc == SQLITE_ROW, "Doc ket qua COUNT(*)");

        if (rc == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            TEST_ASSERT(count == 1, "Ban ghi cua alias logger_log_activity duoc luu thanh cong vao DB");
        }

        sqlite3_finalize(stmt);
    }
}

static void test_table_not_exists(void)
{
    printf("\nTest 6: Xu ly khi bang activity_logs khong ton tai\n");
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    TEST_ASSERT(rc == SQLITE_OK && db != NULL, "Mo database in-memory rieng chua tao bang");

    if (rc == SQLITE_OK && db != NULL) {
        int log_rc = logger_log(db, 1, "ACTION_WITHOUT_TABLE");
        TEST_ASSERT(log_rc == -1, "logger_log tra ve -1 khi bang activity_logs khong ton tai");

        sqlite3_close(db);
    }
}

int main(void)
{
    printf("=========================================\n");
    printf("       RUNNING TESTS: MODULE LOGGER      \n");
    printf("=========================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "Setup test database failed!\n");
        return EXIT_FAILURE;
    }

    test_log_valid_event_with_user(db);
    test_log_valid_event_without_user(db);
    test_invalid_parameters(db);
    test_foreign_key_violation(db);
    test_alias_logger_log_activity(db);
    test_table_not_exists();

    sqlite3_close(db);

    printf("\n=========================================\n");
    printf("Ket qua: %d/%d tests PASSED\n", g_tests_passed, g_tests_run);
    printf("=========================================\n");

    if (g_tests_passed == g_tests_run) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}
