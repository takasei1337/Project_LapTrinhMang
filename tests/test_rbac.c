#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include "rbac.h"
#include "session.h"
#include "auth.h"
#include "question.h"
#include "classification.h"
#include "practice.h"

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

static void exec_sql(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Error: %s\nSQL: %s\n", err_msg ? err_msg : "Unknown", sql);
        sqlite3_free(err_msg);
        exit(EXIT_FAILURE);
    }
}

static int count_rows(sqlite3 *db, const char *table)
{
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return -1;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

static sqlite3 *setup_test_db(void)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open in-memory DB\n");
        exit(EXIT_FAILURE);
    }

    exec_sql(db, "PRAGMA foreign_keys = ON;");

    char *schema_sql = read_file_to_string("database/schema.sql");
    if (!schema_sql) {
        fprintf(stderr, "Cannot read database/schema.sql\n");
        exit(EXIT_FAILURE);
    }
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* Insert test users: User A (1), User B (2), Admin (3) */
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (1, 'user_a', 'hash_a', 'USER');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (2, 'user_b', 'hash_b', 'USER');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (3, 'admin_super', 'hash_admin', 'ADMIN');");

    return db;
}

/*
 * ============================================================================
 * TEST SUITES (TASK 07.2)
 * ============================================================================
 */

/* 1. USER duoc xem danh muc */
static void test_user_can_read_classification(void)
{
    printf("\n--- Test 1: USER duoc xem danh muc ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    int uid = 0;
    int rc = rbac_authorize(db, user_tok, RBAC_ACTION_CLASSIFICATION_READ, &uid);
    TEST_ASSERT(rc == RBAC_PERMITTED, "USER has permission to read classification");
    TEST_ASSERT(uid == 1, "Extracted user_id is 1");

    free(user_tok);
    sqlite3_close(db);
}

/* 2. USER khong duoc tao/sua/xoa danh muc */
static void test_user_cannot_manage_classification(void)
{
    printf("\n--- Test 2: USER khong duoc tao/sua/xoa danh muc ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    int uid = 999;
    int rc = rbac_authorize(db, user_tok, RBAC_ACTION_CLASSIFICATION_MANAGE, &uid);
    TEST_ASSERT(rc == RBAC_ERR_FORBIDDEN, "USER is forbidden to manage classification");
    TEST_ASSERT(uid == 0, "out_user_id is reset to 0 on FORBIDDEN");

    free(user_tok);
    sqlite3_close(db);
}

/* 3. ADMIN duoc quan ly danh muc */
static void test_admin_can_manage_classification(void)
{
    printf("\n--- Test 3: ADMIN duoc quan ly danh muc ---\n");
    sqlite3 *db = setup_test_db();
    char *admin_tok = NULL;
    session_create(db, 3, 3600, &admin_tok);

    int uid = 0;
    int rc_manage = rbac_authorize(db, admin_tok, RBAC_ACTION_CLASSIFICATION_MANAGE, &uid);
    TEST_ASSERT(rc_manage == RBAC_PERMITTED, "ADMIN has permission to manage classification");
    TEST_ASSERT(uid == 3, "Extracted user_id is 3");

    int rc_read = rbac_authorize(db, admin_tok, RBAC_ACTION_CLASSIFICATION_READ, &uid);
    TEST_ASSERT(rc_read == RBAC_PERMITTED, "ADMIN has permission to read classification");

    free(admin_tok);
    sqlite3_close(db);
}

/* 4. USER khong duoc thuc hien Question CRUD quan tri */
static void test_user_cannot_manage_questions(void)
{
    printf("\n--- Test 4: USER khong duoc thuc hien Question CRUD quan tri ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    TEST_ASSERT(rbac_authorize(db, user_tok, RBAC_ACTION_QUESTION_MANAGE, NULL) == RBAC_ERR_FORBIDDEN,
                "USER is forbidden from Question CRUD (create/update/delete)");
    TEST_ASSERT(rbac_authorize(db, user_tok, RBAC_ACTION_QUESTION_ASSIGN_TOPIC, NULL) == RBAC_ERR_FORBIDDEN,
                "USER is forbidden from assigning/unassigning topic to question");
    TEST_ASSERT(rbac_authorize(db, user_tok, RBAC_ACTION_QUESTION_VIEW_UNASSIGNED, NULL) == RBAC_ERR_FORBIDDEN,
                "USER is forbidden from viewing unassigned questions list");

    free(user_tok);
    sqlite3_close(db);
}

/* 5. ADMIN duoc thuc hien Question CRUD */
static void test_admin_can_manage_questions(void)
{
    printf("\n--- Test 5: ADMIN duoc thuc hien Question CRUD ---\n");
    sqlite3 *db = setup_test_db();
    char *admin_tok = NULL;
    session_create(db, 3, 3600, &admin_tok);

    TEST_ASSERT(rbac_authorize(db, admin_tok, RBAC_ACTION_QUESTION_MANAGE, NULL) == RBAC_PERMITTED,
                "ADMIN has permission for Question CRUD");
    TEST_ASSERT(rbac_authorize(db, admin_tok, RBAC_ACTION_QUESTION_ASSIGN_TOPIC, NULL) == RBAC_PERMITTED,
                "ADMIN has permission to assign topic");
    TEST_ASSERT(rbac_authorize(db, admin_tok, RBAC_ACTION_QUESTION_VIEW_UNASSIGNED, NULL) == RBAC_PERMITTED,
                "ADMIN has permission to view unassigned questions");
    TEST_ASSERT(rbac_authorize(db, admin_tok, RBAC_ACTION_QUESTION_READ_PUBLIC, NULL) == RBAC_PERMITTED,
                "ADMIN has permission to view public questions list");

    free(admin_tok);
    sqlite3_close(db);
}

/* 6. USER khong duoc xem dap an dung */
static void test_user_cannot_view_correct_answer(void)
{
    printf("\n--- Test 6: USER khong duoc xem dap an dung ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    int uid = 999;
    int rc = rbac_can_view_question_detail(db, user_tok, &uid);
    TEST_ASSERT(rc == RBAC_ERR_FORBIDDEN, "USER is forbidden from viewing question detail with answers");
    TEST_ASSERT(uid == 0, "out_user_id reset to 0 on FORBIDDEN");

    free(user_tok);
    sqlite3_close(db);
}

/* 7. ADMIN duoc xem chi tiet quan tri */
static void test_admin_can_view_correct_answer(void)
{
    printf("\n--- Test 7: ADMIN duoc xem chi tiet quan tri ---\n");
    sqlite3 *db = setup_test_db();
    char *admin_tok = NULL;
    session_create(db, 3, 3600, &admin_tok);

    int uid = 0;
    int rc = rbac_can_view_question_detail(db, admin_tok, &uid);
    TEST_ASSERT(rc == RBAC_PERMITTED, "ADMIN has permission to view question detail with answers");
    TEST_ASSERT(uid == 3, "out_user_id is 3 (admin)");

    free(admin_tok);
    sqlite3_close(db);
}

/* 8. USER duoc bat dau Practice */
static void test_user_can_start_practice(void)
{
    printf("\n--- Test 8: USER duoc bat dau Practice ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    int uid = 0;
    int rc = rbac_authorize(db, user_tok, RBAC_ACTION_PRACTICE_START, &uid);
    TEST_ASSERT(rc == RBAC_PERMITTED, "USER has permission to start practice");
    TEST_ASSERT(uid == 1, "Extracted user_id is 1");

    free(user_tok);
    sqlite3_close(db);
}

/* 9. User A duoc thao tac tren attempt cua A */
static void test_user_a_can_access_own_attempt(void)
{
    printf("\n--- Test 9: User A duoc thao tac tren attempt cua A ---\n");
    sqlite3 *db = setup_test_db();

    /* Tao attempt thuoc User A (id = 1) */
    exec_sql(db, "INSERT INTO exam_attempts (id, user_id, mode) VALUES (101, 1, 'PRACTICE');");

    char *tok_a = NULL;
    session_create(db, 1, 3600, &tok_a);

    int uid = 0;
    int rc = rbac_check_attempt_ownership(db, tok_a, 101, &uid);
    TEST_ASSERT(rc == RBAC_PERMITTED, "User A is permitted to submit/access own attempt 101");
    TEST_ASSERT(uid == 1, "Extracted user_id is 1");

    free(tok_a);
    sqlite3_close(db);
}

/* 10. User A bi tu choi attempt cua B (va Admin cung bi tu choi attempt nguoi khac khi submit) */
static void test_user_a_denied_attempt_of_user_b(void)
{
    printf("\n--- Test 10: User A bi tu choi attempt cua B ---\n");
    sqlite3 *db = setup_test_db();

    /* Attempt 102 thuoc User B (id = 2) */
    exec_sql(db, "INSERT INTO exam_attempts (id, user_id, mode) VALUES (102, 2, 'PRACTICE');");

    char *tok_a = NULL;
    session_create(db, 1, 3600, &tok_a);

    int uid = 999;
    int rc_a = rbac_check_attempt_ownership(db, tok_a, 102, &uid);
    TEST_ASSERT(rc_a == RBAC_ERR_FORBIDDEN, "User A is forbidden from accessing User B's attempt 102");
    TEST_ASSERT(uid == 0, "out_user_id reset to 0 on FORBIDDEN");

    /* Kiem tra Admin cung bi tu choi theo ma tran quyen (Submit attempt nguoi khac: Deny mac dinh) */
    char *tok_admin = NULL;
    session_create(db, 3, 3600, &tok_admin);
    int rc_admin = rbac_check_attempt_ownership(db, tok_admin, 102, &uid);
    TEST_ASSERT(rc_admin == RBAC_ERR_FORBIDDEN, "Admin is forbidden from submitting another user's attempt");

    free(tok_a);
    free(tok_admin);
    sqlite3_close(db);
}

/* 11. Attempt khong ton tai */
static void test_attempt_not_found(void)
{
    printf("\n--- Test 11: Attempt khong ton tai ---\n");
    sqlite3 *db = setup_test_db();
    char *tok_a = NULL;
    session_create(db, 1, 3600, &tok_a);

    int uid = 999;
    int rc = rbac_check_attempt_ownership(db, tok_a, 999999, &uid);
    TEST_ASSERT(rc == RBAC_ERR_NOT_FOUND, "Non-existent attempt returns RBAC_ERR_NOT_FOUND");
    TEST_ASSERT(uid == 0, "out_user_id reset to 0 on NOT_FOUND");

    free(tok_a);
    sqlite3_close(db);
}

/* 12. Session hop le */
static void test_valid_session(void)
{
    printf("\n--- Test 12: Session hop le ---\n");
    sqlite3 *db = setup_test_db();
    char *token = NULL;
    session_create(db, 1, 3600, &token);

    int uid = 0;
    int rc = rbac_authorize(db, token, RBAC_ACTION_QUESTION_READ_PUBLIC, &uid);
    TEST_ASSERT(rc == RBAC_PERMITTED, "Valid session is authorized");
    TEST_ASSERT(uid == 1, "uid extracted correctly");

    free(token);
    sqlite3_close(db);
}

/* 13. Session het han */
static void test_expired_session(void)
{
    printf("\n--- Test 13: Session het han ---\n");
    sqlite3 *db = setup_test_db();

    /* Tao token hop le */
    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* Chinh sua expires_at ve qua khu */
    char token_hash[65];
    session_sha256_hex(token, 64, token_hash);
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "UPDATE sessions SET expires_at = datetime('now', '-10 seconds') WHERE token_hash = ?;", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, token_hash, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    int uid = 999;
    int rc = rbac_authorize(db, token, RBAC_ACTION_QUESTION_READ_PUBLIC, &uid);
    TEST_ASSERT(rc == RBAC_ERR_UNAUTHORIZED, "Expired session returns RBAC_ERR_UNAUTHORIZED");
    TEST_ASSERT(uid == 0, "uid is reset to 0");

    free(token);
    sqlite3_close(db);
}

/* 14. Session da revoke */
static void test_revoked_session(void)
{
    printf("\n--- Test 14: Session da revoke ---\n");
    sqlite3 *db = setup_test_db();
    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* Logout / revoke */
    session_revoke(db, token);

    int uid = 999;
    int rc = rbac_authorize(db, token, RBAC_ACTION_QUESTION_READ_PUBLIC, &uid);
    TEST_ASSERT(rc == RBAC_ERR_UNAUTHORIZED, "Revoked session returns RBAC_ERR_UNAUTHORIZED");
    TEST_ASSERT(uid == 0, "uid is reset to 0");

    free(token);
    sqlite3_close(db);
}

/* 15. Token gia mao / khong ton tai */
static void test_tampered_token(void)
{
    printf("\n--- Test 15: Token gia mao ---\n");
    sqlite3 *db = setup_test_db();
    const char *fake_tok = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    int uid = 999;
    int rc = rbac_authorize(db, fake_tok, RBAC_ACTION_QUESTION_READ_PUBLIC, &uid);
    TEST_ASSERT(rc == RBAC_ERR_UNAUTHORIZED, "Tampered/unknown token returns RBAC_ERR_UNAUTHORIZED");
    TEST_ASSERT(uid == 0, "uid is reset to 0");

    sqlite3_close(db);
}

/* 16. Role khong hop le trong CSDL */
static void test_invalid_role(void)
{
    printf("\n--- Test 16: Role khong hop le trong CSDL ---\n");
    sqlite3 *db = setup_test_db();

    /* Do co CHECK (role IN ('USER', 'ADMIN')), ta test rbac_check_role_action voi role bat ky */
    TEST_ASSERT(rbac_check_role_action("TEACHER", RBAC_ACTION_CLASSIFICATION_READ) == RBAC_ERR_FORBIDDEN,
                "Unsupported role TEACHER denied by default");
    TEST_ASSERT(rbac_check_role_action("GUEST", RBAC_ACTION_CLASSIFICATION_READ) == RBAC_ERR_FORBIDDEN,
                "Unsupported role GUEST denied by default");
    TEST_ASSERT(rbac_check_role_action("", RBAC_ACTION_CLASSIFICATION_READ) == RBAC_ERR_FORBIDDEN,
                "Empty role denied by default");
    TEST_ASSERT(rbac_check_role_action(NULL, RBAC_ACTION_CLASSIFICATION_READ) == RBAC_ERR_FORBIDDEN,
                "NULL role denied by default");

    sqlite3_close(db);
}

/* 17. User bi xoa khoi he thong */
static void test_deleted_user(void)
{
    printf("\n--- Test 17: User bi xoa ---\n");
    sqlite3 *db = setup_test_db();
    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* Xoa User 1 -> bang sessions bi cascade xoa token */
    exec_sql(db, "DELETE FROM users WHERE id = 1;");

    int uid = 999;
    int rc = rbac_authorize(db, token, RBAC_ACTION_CLASSIFICATION_READ, &uid);
    TEST_ASSERT(rc == RBAC_ERR_UNAUTHORIZED, "Deleted user session returns RBAC_ERR_UNAUTHORIZED");
    TEST_ASSERT(uid == 0, "uid is reset to 0");

    free(token);
    sqlite3_close(db);
}

/* 18. Database error */
static void test_database_error(void)
{
    printf("\n--- Test 18: Database error ---\n");
    int uid = 999;
    TEST_ASSERT(rbac_authorize(NULL, "valid_token", RBAC_ACTION_CLASSIFICATION_READ, &uid) == RBAC_ERR_INVALID_PARAM,
                "NULL db in rbac_authorize returns RBAC_ERR_INVALID_PARAM");
    TEST_ASSERT(uid == 0, "uid is reset to 0");

    TEST_ASSERT(rbac_check_attempt_ownership(NULL, "tok", 1, &uid) == RBAC_ERR_INVALID_PARAM,
                "NULL db in rbac_check_attempt_ownership returns RBAC_ERR_INVALID_PARAM");
    TEST_ASSERT(uid == 0, "uid is reset to 0");
}

/* 19. SQL Injection trong token va attempt_id */
static void test_sql_injection_defense(void)
{
    printf("\n--- Test 19: SQL Injection Defense ---\n");
    sqlite3 *db = setup_test_db();

    const char *sqli_tokens[] = {
        "' OR '1'='1",
        "'; DROP TABLE users; --",
        "\" OR 1=1 --",
        "a' UNION SELECT 1, 'ADMIN' --"
    };

    for (size_t i = 0; i < sizeof(sqli_tokens) / sizeof(sqli_tokens[0]); i++) {
        int uid = 999;
        int rc = rbac_authorize(db, sqli_tokens[i], RBAC_ACTION_QUESTION_MANAGE, &uid);
        TEST_ASSERT(rc == RBAC_ERR_UNAUTHORIZED, "SQL injection in token rejected as RBAC_ERR_UNAUTHORIZED");
        TEST_ASSERT(uid == 0, "uid reset to 0");
    }

    TEST_ASSERT(count_rows(db, "users") == 3, "Bảng users nguyên vẹn 100%");
    sqlite3_close(db);
}

/* 20. Deny by default */
static void test_deny_by_default(void)
{
    printf("\n--- Test 20: Deny by default ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    /* Action khong hop le */
    int rc_unknown = rbac_authorize(db, user_tok, (rbac_action_t)9999, NULL);
    TEST_ASSERT(rc_unknown == RBAC_ERR_FORBIDDEN, "Unknown action code denied by default");

    int rc_zero = rbac_authorize(db, user_tok, RBAC_ACTION_UNKNOWN, NULL);
    TEST_ASSERT(rc_zero == RBAC_ERR_FORBIDDEN, "RBAC_ACTION_UNKNOWN denied by default");

    free(user_tok);
    sqlite3_close(db);
}

/* 21. Khong co thay doi database khi kiem tra quyen that bai */
static void test_no_database_mutation_on_check(void)
{
    printf("\n--- Test 21: Khong co thay doi database khi kiem tra quyen that bai ---\n");
    sqlite3 *db = setup_test_db();
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok);

    int u_before = count_rows(db, "users");
    int s_before = count_rows(db, "sessions");
    int a_before = count_rows(db, "exam_attempts");

    /* Thuc hien hang loat hanh vi bi tu choi */
    rbac_authorize(db, user_tok, RBAC_ACTION_QUESTION_MANAGE, NULL);
    rbac_authorize(db, user_tok, RBAC_ACTION_CLASSIFICATION_MANAGE, NULL);
    rbac_authorize(db, "fake_token_1234567890123456789012345678901234567890123456789012345678", RBAC_ACTION_PRACTICE_START, NULL);
    rbac_check_attempt_ownership(db, user_tok, 999999, NULL);

    int u_after = count_rows(db, "users");
    int s_after = count_rows(db, "sessions");
    int a_after = count_rows(db, "exam_attempts");

    TEST_ASSERT(u_before == u_after, "Users count unchanged");
    TEST_ASSERT(s_before == s_after, "Sessions count unchanged");
    TEST_ASSERT(a_before == a_after, "Exam attempts count unchanged");

    free(user_tok);
    sqlite3_close(db);
}

/* 22. End-to-end Regression: Auth -> Session -> RBAC -> Practice & Question */
static void test_e2e_integration(void)
{
    printf("\n--- Test 22: End-to-end Regression Integration ---\n");
    sqlite3 *db = setup_test_db();

    /* 1. Register candidate user */
    int reg_rc = auth_register(db, "candidate_tom", "TomPassword123!");
    TEST_ASSERT(reg_rc == AUTH_SUCCESS, "Register candidate_tom succeeds");

    int user_id = auth_login(db, "candidate_tom", "TomPassword123!");
    TEST_ASSERT(user_id > 0, "Login candidate_tom succeeds");

    /* 2. Create Session */
    char *token = NULL;
    int s_rc = session_create(db, user_id, 3600, &token);
    TEST_ASSERT(s_rc == SESSION_SUCCESS && token != NULL, "Session created");

    /* 3. Check RBAC: Can start practice? Yes */
    int auth_uid = 0;
    int auth_rc = rbac_authorize(db, token, RBAC_ACTION_PRACTICE_START, &auth_uid);
    TEST_ASSERT(auth_rc == RBAC_PERMITTED && auth_uid == user_id, "Candidate authorized to start practice");

    /* 4. Check RBAC: Can manage questions? No */
    int manage_rc = rbac_authorize(db, token, RBAC_ACTION_QUESTION_MANAGE, NULL);
    TEST_ASSERT(manage_rc == RBAC_ERR_FORBIDDEN, "Candidate forbidden from managing questions");

    /* 5. Create attempt for candidate_tom and verify ownership */
    exec_sql(db, "INSERT INTO exam_attempts (id, user_id, mode) VALUES (501, 4, 'PRACTICE');");
    int own_uid = 0;
    int own_rc = rbac_check_attempt_ownership(db, token, 501, &own_uid);
    TEST_ASSERT(own_rc == RBAC_PERMITTED && own_uid == user_id, "Candidate owns attempt 501");

    /* 6. Logout and verify RBAC immediately denies */
    session_revoke(db, token);
    int after_logout = rbac_authorize(db, token, RBAC_ACTION_PRACTICE_START, NULL);
    TEST_ASSERT(after_logout == RBAC_ERR_UNAUTHORIZED, "Revoked session immediately rejected by RBAC");

    free(token);
    sqlite3_close(db);
}

int main(void)
{
    printf("====================================================================\n");
    printf("UNIT TESTS: ROLE-BASED ACCESS CONTROL (TASK 07.2)\n");
    printf("====================================================================\n");

    test_user_can_read_classification();
    test_user_cannot_manage_classification();
    test_admin_can_manage_classification();
    test_user_cannot_manage_questions();
    test_admin_can_manage_questions();
    test_user_cannot_view_correct_answer();
    test_admin_can_view_correct_answer();
    test_user_can_start_practice();
    test_user_a_can_access_own_attempt();
    test_user_a_denied_attempt_of_user_b();
    test_attempt_not_found();
    test_valid_session();
    test_expired_session();
    test_revoked_session();
    test_tampered_token();
    test_invalid_role();
    test_deleted_user();
    test_database_error();
    test_sql_injection_defense();
    test_deny_by_default();
    test_no_database_mutation_on_check();
    test_e2e_integration();

    printf("\n====================================================================\n");
    printf("RESULT: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("====================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}

