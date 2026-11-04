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
 * Test 1 — Dang nhap thanh cong
 * ========================================================================= */
static void test_login_success(sqlite3 *db)
{
    printf("\nTest 1 — Dang nhap thanh cong voi tai khoan da dang ky\n");

    const char *username = "alice";
    const char *password = "AliceSecurePass123!";

    int reg_rc = auth_register(db, username, password);
    TEST_ASSERT(reg_rc == AUTH_SUCCESS, "Dang ky alice thanh cong qua auth_register");

    int login_rc = auth_login(db, username, password);
    TEST_ASSERT(login_rc > 0, "auth_login tra ve user_id duong (> 0)");

    /* Lay user_id thuc te tu CSDL */
    const char *query = "SELECT id FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement SELECT id");

    int db_id = -1;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            db_id = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    TEST_ASSERT(db_id > 0, "Doc duoc id cua alice tu CSDL");
    TEST_ASSERT(login_rc == db_id, "user_id tra ve boi auth_login khop chinh xac voi id trong CSDL");
}

/* =========================================================================
 * Test 2 — Mat khau sai
 * ========================================================================= */
static void test_login_wrong_password(sqlite3 *db)
{
    printf("\nTest 2 — Dang nhap voi mat khau sai\n");

    const char *username = "alice";
    const char *wrong_password = "WrongPassword999!";

    int rc = auth_login(db, username, wrong_password);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_CREDENTIALS,
                "auth_login tra ve AUTH_ERR_INVALID_CREDENTIALS (-5) khi sai mat khau");

    /* Kiem tra du lieu tai khoan khong bi thay doi */
    const char *query = "SELECT password_hash, role FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    int q_rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
    TEST_ASSERT(q_rc == SQLITE_OK && stmt != NULL, "Chuan bi statement kiem tra alice");

    if (q_rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *role = sqlite3_column_text(stmt, 1);
            TEST_ASSERT(role != NULL && strcmp((const char *)role, "USER") == 0,
                        "Vai tro cua tai khoan khong bi anh huong boi dang nhap sai");
        }
        sqlite3_finalize(stmt);
    }
}

/* =========================================================================
 * Test 3 — Username khong ton tai
 * ========================================================================= */
static void test_login_nonexistent_user(sqlite3 *db)
{
    printf("\nTest 3 — Dang nhap voi username khong ton tai\n");

    const char *non_existent_user = "ghost_user";
    const char *password = "ValidPassword123!";

    /* Dem so luong user truoc khi test */
    int count_before = 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count_before = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    int rc = auth_login(db, non_existent_user, password);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_CREDENTIALS,
                "auth_login tra ve AUTH_ERR_INVALID_CREDENTIALS (-5) khi username khong ton tai");

    /* Dem so luong user sau khi test */
    int count_after = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count_after = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    TEST_ASSERT(count_before == count_after,
                "Khong co tai khoan moi nao bi tao khi dang nhap that bai");
}

/* =========================================================================
 * Test 4 — Validation tham so
 * ========================================================================= */
static void test_login_validation(sqlite3 *db)
{
    printf("\nTest 4 — Kiem tra tinh hop le cua tham so dau vao (Validation)\n");

    /* 1. db == NULL */
    int rc = auth_login(NULL, "alice", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "db == NULL bi tu choi voi AUTH_ERR_INVALID_PARAM");

    /* 2. username == NULL */
    rc = auth_login(db, NULL, "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username == NULL bi tu choi voi AUTH_ERR_INVALID_PARAM");

    /* 3. password == NULL */
    rc = auth_login(db, "alice", NULL);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password == NULL bi tu choi voi AUTH_ERR_INVALID_PARAM");

    /* 4. username rong "" */
    rc = auth_login(db, "", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username rong \"\" bi tu choi");

    /* 5. username < 3 ky tu */
    rc = auth_login(db, "ab", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username 'ab' (2 ky tu) bi tu choi");

    /* 6. username > 32 ky tu (33 ky tu) */
    rc = auth_login(db, "a12345678901234567890123456789012", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username 33 ky tu bi tu choi");

    /* 7. username chua khoang trang */
    rc = auth_login(db, "alice smith", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua khoang trang bi tu choi");

    /* 8. username chua ky tu dac biet */
    rc = auth_login(db, "alice@work", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '@' bi tu choi");

    rc = auth_login(db, "alice-work", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '-' bi tu choi");

    rc = auth_login(db, "alice.work", "AliceSecurePass123!");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "username chua '.' bi tu choi");

    /* 9. password rong "" */
    rc = auth_login(db, "alice", "");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password rong \"\" bi tu choi");

    /* 10. password < 8 byte (7 byte) */
    rc = auth_login(db, "alice", "1234567");
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password 7 byte bi tu choi");

    /* 11. password > 128 byte (129 byte) */
    char long_pwd[130];
    memset(long_pwd, 'P', 129);
    long_pwd[129] = '\0';
    rc = auth_login(db, "alice", long_pwd);
    TEST_ASSERT(rc == AUTH_ERR_INVALID_PARAM, "password 129 byte bi tu choi");
}

/* =========================================================================
 * Test 5 — Dang nhap nhieu lan
 * ========================================================================= */
static void test_login_multiple_times(sqlite3 *db)
{
    printf("\nTest 5 — Dang nhap nhieu lan lien tiep voi cung tai khoan\n");

    const char *username = "alice";
    const char *password = "AliceSecurePass123!";

    int id1 = auth_login(db, username, password);
    int id2 = auth_login(db, username, password);
    int id3 = auth_login(db, username, password);

    TEST_ASSERT(id1 > 0, "Lan 1 dang nhap thanh cong");
    TEST_ASSERT(id2 > 0, "Lan 2 dang nhap thanh cong");
    TEST_ASSERT(id3 > 0, "Lan 3 dang nhap thanh cong");
    TEST_ASSERT(id1 == id2 && id2 == id3, "Tat ca cac lan deu tra ve cung mot user_id");

    /* Kiem tra khong tao them ban ghi */
    const char *query = "SELECT COUNT(*) FROM users WHERE username = 'alice';";
    sqlite3_stmt *stmt = NULL;
    int count = -1;
    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(count == 1, "So luong ban ghi cua alice van duy nhat la 1");
}

/* =========================================================================
 * Test 6 — Hai tai khoan dung cung mat khau
 * ========================================================================= */
static void test_two_users_same_password(sqlite3 *db)
{
    printf("\nTest 6 — Hai tai khoan su dung cung mot mat khau\n");

    const char *shared_password = "SharedPassword2026!";

    int rc1 = auth_register(db, "user1", shared_password);
    int rc2 = auth_register(db, "user2", shared_password);
    TEST_ASSERT(rc1 == AUTH_SUCCESS, "Dang ky user1 thanh cong");
    TEST_ASSERT(rc2 == AUTH_SUCCESS, "Dang ky user2 thanh cong");

    int id1 = auth_login(db, "user1", shared_password);
    int id2 = auth_login(db, "user2", shared_password);

    TEST_ASSERT(id1 > 0, "Dang nhap user1 thanh cong");
    TEST_ASSERT(id2 > 0, "Dang nhap user2 thanh cong");
    TEST_ASSERT(id1 != id2, "Hai tai khoan co hai user_id khac biet nhau");

    /* Kiem tra cheo: username khong the dang nhap neu truyen nham ten */
    int wrong_cross = auth_login(db, "user1_nonexistent", shared_password);
    TEST_ASSERT(wrong_cross == AUTH_ERR_INVALID_CREDENTIALS,
                "Khong the nham lan tai khoan");
}

/* =========================================================================
 * Test 7 — Database loi
 * ========================================================================= */
static void test_login_database_error(void)
{
    printf("\nTest 7 — Xu ly loi database (chua tao bang users)\n");

    sqlite3 *db_empty = NULL;
    int rc = sqlite3_open(":memory:", &db_empty);
    TEST_ASSERT(rc == SQLITE_OK && db_empty != NULL, "Mo database in-memory moi chua co bang");

    if (rc == SQLITE_OK && db_empty != NULL) {
        int log_rc = auth_login(db_empty, "some_user", "ValidPassword123!");
        TEST_ASSERT(log_rc == AUTH_ERR_DATABASE,
                    "auth_login tra ve AUTH_ERR_DATABASE (-4) khi khong co bang users");
        sqlite3_close(db_empty);
    }
}

/* =========================================================================
 * Test 8 — Hash bi hong trong CSDL
 * ========================================================================= */
static void test_corrupted_hash(sqlite3 *db)
{
    printf("\nTest 8 — Xu ly cac truong hop password_hash bi hong trong CSDL\n");

    /* 8.1 Hash rong "" */
    const char *sql_empty = "INSERT INTO users (username, password_hash) VALUES ('empty_hash_u', '');";
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql_empty, NULL, NULL, &err_msg);
    TEST_ASSERT(rc == SQLITE_OK, "Chen tai khoan empty_hash_u voi hash rong");

    int log_rc = auth_login(db, "empty_hash_u", "ValidPassword123!");
    TEST_ASSERT(log_rc == AUTH_ERR_HASH_FAILED,
                "auth_login tra ve AUTH_ERR_HASH_FAILED (-3) khi hash rong");

    /* 8.2 Hash khong dung dinh dang Argon2id */
    const char *sql_malformed = "INSERT INTO users (username, password_hash) "
                                "VALUES ('bad_hash_u', 'this_is_not_an_argon2id_hash_format');";
    rc = sqlite3_exec(db, sql_malformed, NULL, NULL, &err_msg);
    TEST_ASSERT(rc == SQLITE_OK, "Chen tai khoan bad_hash_u voi hash sai dinh dang");

    log_rc = auth_login(db, "bad_hash_u", "ValidPassword123!");
    TEST_ASSERT(log_rc == AUTH_ERR_HASH_FAILED,
                "auth_login tra ve AUTH_ERR_HASH_FAILED (-3) khi hash sai dinh dang");

    /* 8.3 Hash dai hon 255 byte */
    char long_hash[270];
    memset(long_hash, 'X', 265);
    long_hash[265] = '\0';

    const char *sql_long = "INSERT INTO users (username, password_hash) VALUES ('long_hash_u', ?);";
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_long, -1, &stmt, NULL);
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_text(stmt, 1, long_hash, -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(rc == SQLITE_DONE, "Chen tai khoan long_hash_u voi hash 265 byte");

    log_rc = auth_login(db, "long_hash_u", "ValidPassword123!");
    TEST_ASSERT(log_rc == AUTH_ERR_HASH_FAILED,
                "auth_login tra ve AUTH_ERR_HASH_FAILED (-3) khi hash > 255 byte ma khong tran bo dem");

    /* 8.4 Hash chua byte NUL giua chuoi */
    const char *sql_nul = "INSERT INTO users (username, password_hash) VALUES ('nul_hash_u', ?);";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_nul, -1, &stmt, NULL);
    if (rc == SQLITE_OK && stmt != NULL) {
        const char raw_nul_data[] = "$argon2id$v=19\0m=65536,t=2$invalid";
        sqlite3_bind_text(stmt, 1, raw_nul_data, sizeof(raw_nul_data), SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(rc == SQLITE_DONE, "Chen tai khoan nul_hash_u voi hash chua byte NUL giua chuoi");

    log_rc = auth_login(db, "nul_hash_u", "ValidPassword123!");
    TEST_ASSERT(log_rc == AUTH_ERR_HASH_FAILED,
                "auth_login tra ve AUTH_ERR_HASH_FAILED (-3) khi hash chua NUL giua chuoi va khong crash");

    /* 8.5 Hash la NULL: Schema bat buoc NOT NULL nen viec chen NULL phai bi SQLite ngan chan */
    const char *sql_null_hash = "INSERT INTO users (username, password_hash) VALUES ('null_hash_u', NULL);";
    rc = sqlite3_exec(db, sql_null_hash, NULL, NULL, &err_msg);
    TEST_ASSERT(rc == SQLITE_CONSTRAINT,
                "Rang buoc NOT NULL ngan chan chen password_hash NULL");
    if (err_msg) {
        sqlite3_free(err_msg);
    }
}

/* =========================================================================
 * Test 9 — Bao toan du lieu va role
 * ========================================================================= */
static void test_role_and_data_preservation(sqlite3 *db)
{
    printf("\nTest 9 — Bao toan du lieu va vai tro (role) nguoi dung\n");

    /* Tao tai khoan ADMIN voi mat khau hop le */
    const char *admin_user = "admin_acc";
    const char *admin_pass = "AdminSuperPass123!";

    int reg_rc = auth_register(db, admin_user, admin_pass);
    TEST_ASSERT(reg_rc == AUTH_SUCCESS, "Dang ky admin_acc thanh cong");

    /* Cap nhat role thanh ADMIN */
    const char *update_sql = "UPDATE users SET role = 'ADMIN' WHERE username = 'admin_acc';";
    int u_rc = sqlite3_exec(db, update_sql, NULL, NULL, NULL);
    TEST_ASSERT(u_rc == SQLITE_OK, "Cap nhat role cua admin_acc thanh 'ADMIN'");

    /* Dang nhap voi admin_acc */
    int admin_id = auth_login(db, admin_user, admin_pass);
    TEST_ASSERT(admin_id > 0, "Dang nhap tai khoan ADMIN thanh cong voi user_id hop le");

    /* Dang nhap voi tai khoan USER alice */
    int alice_id = auth_login(db, "alice", "AliceSecurePass123!");
    TEST_ASSERT(alice_id > 0, "Dang nhap tai khoan USER alice thanh cong");

    /* Kiem tra role trong CSDL van duoc giu nguyen */
    const char *q_admin = "SELECT role FROM users WHERE username = 'admin_acc';";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, q_admin, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *role = sqlite3_column_text(stmt, 0);
            TEST_ASSERT(role != NULL && strcmp((const char *)role, "ADMIN") == 0,
                        "Role cua admin_acc van la 'ADMIN', khong bi thay doi");
        }
        sqlite3_finalize(stmt);
    }

    const char *q_alice = "SELECT role FROM users WHERE username = 'alice';";
    stmt = NULL;
    if (sqlite3_prepare_v2(db, q_alice, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *role = sqlite3_column_text(stmt, 0);
            TEST_ASSERT(role != NULL && strcmp((const char *)role, "USER") == 0,
                        "Role cua alice van la 'USER', khong bi thay doi");
        }
        sqlite3_finalize(stmt);
    }
}

/* =========================================================================
 * Main Entry Point
 * ========================================================================= */
int main(void)
{
    printf("=====================================================\n");
    printf("         UNIT TEST: MODULE AUTH LOGIN (ARGON2ID)     \n");
    printf("=====================================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "Fatal error: Setup test database failed!\n");
        return EXIT_FAILURE;
    }

    test_login_success(db);
    test_login_wrong_password(db);
    test_login_nonexistent_user(db);
    test_login_validation(db);
    test_login_multiple_times(db);
    test_two_users_same_password(db);
    test_login_database_error();
    test_corrupted_hash(db);
    test_role_and_data_preservation(db);

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

