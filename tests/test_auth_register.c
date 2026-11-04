#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <argon2.h>

#include "auth.h"

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
    if (rc != SQLITE_OK || !db) {
        fprintf(stderr, "Cannot open in-memory database\n");
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

    return db;
}

/* =========================================================================
 * Test 1 — Dang ky thanh cong
 * ========================================================================= */
static void test_register_success(sqlite3 *db)
{
    printf("\nTest 1 — Dang ky thanh cong tai khoan hop le\n");

    const char *username = "alice";
    const char *raw_password = "SecurePass123!";

    int rc = auth_register(db, username, raw_password);
    TEST_ASSERT(rc == AUTH_SUCCESS, "auth_register tra ve AUTH_SUCCESS (0)");

    const char *query = "SELECT id, username, password_hash, role, created_at FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT alice thanh cong");

    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        TEST_ASSERT(rc == SQLITE_ROW, "Tim thay dung 1 ban ghi cua alice trong CSDL");

        if (rc == SQLITE_ROW) {
            const unsigned char *db_user = sqlite3_column_text(stmt, 1);
            const unsigned char *db_hash = sqlite3_column_text(stmt, 2);
            const unsigned char *db_role = sqlite3_column_text(stmt, 3);
            const unsigned char *db_created = sqlite3_column_text(stmt, 4);

            TEST_ASSERT(db_user != NULL && strcmp((const char *)db_user, "alice") == 0,
                        "Username trong CSDL dung bang 'alice'");
            TEST_ASSERT(db_role != NULL && strcmp((const char *)db_role, "USER") == 0,
                        "Vai tro mac dinh dung bang 'USER'");
            TEST_ASSERT(db_created != NULL && strlen((const char *)db_created) > 0,
                        "created_at duoc tao tu dong va khong NULL");
            TEST_ASSERT(db_hash != NULL && strlen((const char *)db_hash) > 0,
                        "password_hash khong rong va khong NULL");
            TEST_ASSERT(db_hash != NULL && strncmp((const char *)db_hash, "$argon2id$v=19$", 15) == 0,
                        "password_hash bat dau bang tien to chuan '$argon2id$v=19$'");
            TEST_ASSERT(db_hash != NULL && strcmp((const char *)db_hash, raw_password) != 0,
                        "password_hash khong bang mat khau plaintext");
        }
        sqlite3_finalize(stmt);
    }
}

/* =========================================================================
 * Test 2 — Xac minh Argon2id
 * ========================================================================= */
static void test_argon2id_verification(sqlite3 *db)
{
    printf("\nTest 2 — Xac minh tinh dung dan cua ma bam Argon2id\n");

    /* Lay hash cua alice da tao o Test 1 */
    const char *query = "SELECT password_hash FROM users WHERE username = 'alice';";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi SELECT hash cua alice");

    char alice_hash[256] = {0};
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *h = sqlite3_column_text(stmt, 0);
            if (h) {
                strncpy(alice_hash, (const char *)h, sizeof(alice_hash) - 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    TEST_ASSERT(strlen(alice_hash) > 0, "Doc thanh cong hash cua alice");

    /* Xac minh voi dung mat khau */
    int v_ok = argon2id_verify(alice_hash, "SecurePass123!", strlen("SecurePass123!"));
    TEST_ASSERT(v_ok == ARGON2_OK, "argon2id_verify chap nhan mat khau dung (ARGON2_OK)");

    /* Xac minh voi sai mat khau */
    int v_fail = argon2id_verify(alice_hash, "WrongPassword999!", strlen("WrongPassword999!"));
    TEST_ASSERT(v_fail != ARGON2_OK, "argon2id_verify tu choi mat khau sai");

    /* Dang ky tai khoan khac voi cung mat khau */
    int rc_charlie = auth_register(db, "charlie", "SecurePass123!");
    TEST_ASSERT(rc_charlie == AUTH_SUCCESS, "Dang ky tai khoan charlie voi cung mat khau thanh cong");

    query = "SELECT password_hash FROM users WHERE username = 'charlie';";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    char charlie_hash[256] = {0};
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *h = sqlite3_column_text(stmt, 0);
            if (h) {
                strncpy(charlie_hash, (const char *)h, sizeof(charlie_hash) - 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    TEST_ASSERT(strlen(charlie_hash) > 0, "Doc thanh cong hash cua charlie");
    TEST_ASSERT(strcmp(alice_hash, charlie_hash) != 0,
                "Hai tai khoan cung mat khau co hash khac nhau nho salt ngau nhien");

    int v_charlie = argon2id_verify(charlie_hash, "SecurePass123!", strlen("SecurePass123!"));
    TEST_ASSERT(v_charlie == ARGON2_OK, "argon2id_verify cua charlie thanh cong voi mat khau dung");
}

/* =========================================================================
 * Test 3 — Username trung lap
 * ========================================================================= */
static void test_duplicate_username(sqlite3 *db)
{
    printf("\nTest 3 — Xu ly username trung lap\n");

    int rc1 = auth_register(db, "bob", "BobPassFirst123!");
    TEST_ASSERT(rc1 == AUTH_SUCCESS, "Dang ky bob lan dau thanh cong");

    /* Lay hash ban dau cua bob */
    const char *query = "SELECT password_hash FROM users WHERE username = 'bob';";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    char original_hash[256] = {0};
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *h = sqlite3_column_text(stmt, 0);
            if (h) {
                strncpy(original_hash, (const char *)h, sizeof(original_hash) - 1);
            }
        }
        sqlite3_finalize(stmt);
    }

    /* Dang ky bob lan 2 voi mat khau khac */
    int rc2 = auth_register(db, "bob", "BobPassSecond456!");
    TEST_ASSERT(rc2 == AUTH_ERR_USER_EXISTS,
                "auth_register tra ve AUTH_ERR_USER_EXISTS (-2) khi username da ton tai");

    /* Kiem tra chi co dung 1 ban ghi bob */
    query = "SELECT COUNT(*), password_hash FROM users WHERE username = 'bob';";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement dem so luong user bob");

    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int count = sqlite3_column_int(stmt, 0);
            const unsigned char *h = sqlite3_column_text(stmt, 1);

            TEST_ASSERT(count == 1, "Database chi co dung duy nhat 1 tai khoan bob");
            TEST_ASSERT(h != NULL && strcmp((const char *)h, original_hash) == 0,
                        "Mat khau hash ban dau cua bob khong bi ghi de hoac thay doi");
        }
        sqlite3_finalize(stmt);
    }
}

/* =========================================================================
 * Test 4 — Validation tham so
 * ========================================================================= */
static void test_input_validation(sqlite3 *db)
{
    printf("\nTest 4 — Kiem tra tinh hop le cua tham so dau vao (Validation)\n");

    /* 1. db == NULL */
    int rc = auth_register(NULL, "valid_user", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "db == NULL bi tu choi voi AUTH_ERR_INVALID_PARAM");

    /* 2. username == NULL */
    rc = auth_register(db, NULL, "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username == NULL bi tu choi voi AUTH_ERR_INVALID_PARAM");

    /* 3. username rong */
    rc = auth_register(db, "", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username rong \"\" bi tu choi");

    /* 4. username duoi 3 ky tu */
    rc = auth_register(db, "ab", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username 'ab' (2 ky tu) bi tu choi");

    /* 5. username tren 32 ky tu (33 ky tu) */
    rc = auth_register(db, "a12345678901234567890123456789012", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username 33 ky tu bi tu choi");

    /* 6. username chua khoang trang */
    rc = auth_register(db, "alice smith", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua khoang trang bi tu choi");

    /* 7. username chua '@', '-', '.' */
    rc = auth_register(db, "alice@work", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '@' bi tu choi");

    rc = auth_register(db, "alice-work", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '-' bi tu choi");

    rc = auth_register(db, "alice.work", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '.' bi tu choi");

    /* 8. username chua ky tu UTF-8 ngoai ASCII */
    rc = auth_register(db, "alice_đẹp", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua UTF-8 ngoai ASCII bi tu choi");

    /* 9. password == NULL */
    rc = auth_register(db, "valid_user_x1", NULL);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password == NULL bi tu choi");

    /* 10. password rong */
    rc = auth_register(db, "valid_user_x2", "");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password rong bi tu choi");

    /* 11. password duoi 8 byte (7 byte) */
    rc = auth_register(db, "valid_user_x3", "1234567");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password 7 byte bi tu choi");

    /* 12. password tren 128 byte (129 byte) */
    char long_pwd[130];
    memset(long_pwd, 'A', 129);
    long_pwd[129] = '\0';
    rc = auth_register(db, "valid_user_x4", long_pwd);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password 129 byte bi tu choi");

    /* 13. Bien hop le: username dai dung 3 ky tu */
    rc = auth_register(db, "usr", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_SUCCESS, "username dai dung 3 ky tu hop le duoc chap nhan");

    /* 14. Bien hop le: username dai dung 32 ky tu */
    rc = auth_register(db, "u1234567890123456789012345678901", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_SUCCESS, "username dai dung 32 ky tu hop le duoc chap nhan");

    /* 15. Bien hop le: password dai dung 8 byte */
    rc = auth_register(db, "user_pwd8", "12345678");
    TEST_ASSERT(rc == AUTH_SUCCESS, "password dai dung 8 byte hop le duoc chap nhan");

    /* 16. Bien hop le: password dai dung 128 byte */
    char pwd128[129];
    memset(pwd128, 'B', 128);
    pwd128[128] = '\0';
    rc = auth_register(db, "user_pwd128", pwd128);
    TEST_ASSERT(rc == AUTH_SUCCESS, "password dai dung 128 byte hop le duoc chap nhan");
}

/* =========================================================================
 * Test 5 — Xu ly loi database
 * ========================================================================= */
static void test_database_error(void)
{
    printf("\nTest 5 — Xu ly loi co so du lieu (bang users chua ton tai)\n");

    sqlite3 *db_empty = NULL;
    int rc = sqlite3_open(":memory:", &db_empty);
    TEST_ASSERT(rc == SQLITE_OK && db_empty != NULL, "Mo database in-memory rieng chua co bang users");

    if (rc == SQLITE_OK && db_empty != NULL) {
        int reg_rc = auth_register(db_empty, "test_db_err", "ValidPassword123!");
        TEST_ASSERT(reg_rc == AUTH_ERR_DATABASE,
                    "auth_register tra ve AUTH_ERR_DATABASE (-4) va khong gay crash");

        sqlite3_close(db_empty);
    }
}

/* =========================================================================
 * Test 6 — Kiem tra vai tro mac dinh (Role)
 * ========================================================================= */
static void test_role_security(sqlite3 *db)
{
    printf("\nTest 6 — Kiem tra tinh bat bien cua vai tro mac dinh (Role = 'USER')\n");

    /* Dang ky tai khoan voi username chua chuoi 'admin' */
    int rc = auth_register(db, "admin", "AdminPass123!");
    TEST_ASSERT(rc == AUTH_SUCCESS, "Dang ky username 'admin' thanh cong");

    const char *query = "SELECT role FROM users WHERE username = 'admin';";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi SELECT role cua username 'admin'");

    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *role = sqlite3_column_text(stmt, 0);
            TEST_ASSERT(role != NULL && strcmp((const char *)role, "USER") == 0,
                        "Username 'admin' van co vai tro 'USER', khong bi leo thang thanh 'ADMIN'");
        }
        sqlite3_finalize(stmt);
    }

    /* Dang ky voi mat khau chua cum tu ADMIN */
    rc = auth_register(db, "user_role_test", "ROLE_ADMIN_SECRET!");
    TEST_ASSERT(rc == AUTH_SUCCESS, "Dang ky voi mat khau chua chuoi role thanh cong");

    query = "SELECT role FROM users WHERE username = 'user_role_test';";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *role = sqlite3_column_text(stmt, 0);
            TEST_ASSERT(role != NULL && strcmp((const char *)role, "USER") == 0,
                        "Tai khoan co password chua role van duoc luu vai tro 'USER'");
        }
        sqlite3_finalize(stmt);
    }
}

/* =========================================================================
 * Test 7 — An toan du lieu
 * ========================================================================= */
static void test_data_safety(sqlite3 *db)
{
    printf("\nTest 7 — An toan du lieu va chong SQL Injection\n");

    /* 1. Kiem tra cac chuoi SQL Injection pho bien bi chan boi validation */
    int rc = auth_register(db, "' OR 1=1 --", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "Username chua SQL Injection \"' OR 1=1 --\" bi tu choi");

    rc = auth_register(db, "admin; DROP TABLE users; --", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "Username chua lenh DROP TABLE bi tu choi");

    rc = auth_register(db, "\";--", "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "Username chua ky tu dau ngoac kep va comment SQL bi tu choi");

    /* 2. Kiem tra tinh toan ven cua username luu vao DB */
    const char *target_user = "user_precise_name";
    rc = auth_register(db, target_user, "ValidPassword123!");
    TEST_ASSERT(rc == AUTH_SUCCESS, "Dang ky target_user thanh cong");

    const char *query = "SELECT username FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_text(stmt, 1, target_user, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *saved_u = sqlite3_column_text(stmt, 0);
            TEST_ASSERT(saved_u != NULL && strcmp((const char *)saved_u, target_user) == 0,
                        "Username trong DB khop hoan toan voi username dau vao, khong bi bien doi");
        }
        sqlite3_finalize(stmt);
    }

    /* 3. Kiem tra toan bo cot password_hash khong chua mat khau plaintext */
    query = "SELECT password_hash FROM users;";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi quet toan bo cot password_hash");

    int all_hashes_valid = 1;
    if (rc == SQLITE_OK && stmt != NULL) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *h = sqlite3_column_text(stmt, 0);
            if (!h || strncmp((const char *)h, "$argon2id$v=19$", 15) != 0) {
                all_hashes_valid = 0;
                break;
            }
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(all_hashes_valid == 1,
                "Toan bo cac ban ghi deu duoc ma hoa bang Argon2id, khong luu plaintext");
}

/* =========================================================================
 * Main entry point
 * ========================================================================= */
int main(void)
{
    printf("=====================================================\n");
    printf("       UNIT TEST: MODULE AUTH REGISTER (ARGON2ID)    \n");
    printf("=====================================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "Fatal error: Setup test database failed!\n");
        return EXIT_FAILURE;
    }

    test_register_success(db);
    test_argon2id_verification(db);
    test_duplicate_username(db);
    test_input_validation(db);
    test_database_error();
    test_role_security(db);
    test_data_safety(db);

    sqlite3_close(db);

    printf("\n=====================================================\n");
    printf("Ket qua: %d/%d tests PASSED\n", g_tests_passed, g_tests_run);
    printf("=====================================================\n");

    if (g_tests_passed == g_tests_run) {
        return EXIT_SUCCESS;
    } else {
        return EXIT_FAILURE;
    }
}

