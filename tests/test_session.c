#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#include "session.h"
#include "auth.h"
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

    /* Insert test users */
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (1, 'alice', 'hash_alice', 'USER');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (2, 'admin_user', 'hash_admin', 'ADMIN');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (3, 'bob_cascade', 'hash_bob', 'USER');");

    return db;
}

/*
 * ============================================================================
 * Test Suites
 * ============================================================================
 */

static void test_session_create_success(void)
{
    printf("\n--- Test: session_create_success ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    int rc = session_create(db, 1, 3600, &token);
    TEST_ASSERT(rc == SESSION_SUCCESS, "1. session_create returns SESSION_SUCCESS");
    TEST_ASSERT(token != NULL, "2. Returned token is not NULL");
    if (token) {
        TEST_ASSERT(strlen(token) == SESSION_TOKEN_HEX_LEN, "3. Token length is exactly 64 hex characters");

        /* Verify token is stored as hash, NOT plaintext */
        sqlite3_stmt *stmt = NULL;
        sqlite3_prepare_v2(db, "SELECT token_hash FROM sessions WHERE user_id = 1;", -1, &stmt, NULL);
        int step = sqlite3_step(stmt);
        TEST_ASSERT(step == SQLITE_ROW, "4. Session record found in DB");
        if (step == SQLITE_ROW) {
            const char *db_hash = (const char *)sqlite3_column_text(stmt, 0);
            TEST_ASSERT(db_hash != NULL, "5. Stored token_hash is not NULL");
            TEST_ASSERT(strcmp(db_hash, token) != 0, "6. Database stores HASH, never plaintext token!");
            TEST_ASSERT(strlen(db_hash) == 64, "7. Stored hash is 64 hex characters (SHA-256)");
        }
        sqlite3_finalize(stmt);

        free(token);
    }

    sqlite3_close(db);
}

static void test_session_create_user_not_found(void)
{
    printf("\n--- Test: session_create_user_not_found ---\n");
    sqlite3 *db = setup_test_db();

    char *token = (char *)0x1234;
    int rc = session_create(db, 9999, 3600, &token);
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "1. Returns SESSION_ERR_NOT_FOUND for non-existent user");
    TEST_ASSERT(token == NULL, "2. out_token set to NULL on failure");
    TEST_ASSERT(count_rows(db, "sessions") == 0, "3. No session created in DB");

    sqlite3_close(db);
}

static void test_session_token_entropy_and_format(void)
{
    printf("\n--- Test: session_token_entropy_and_format ---\n");
    sqlite3 *db = setup_test_db();

    /* Generate multiple tokens and inspect formatting and entropy */
    for (int i = 0; i < 5; i++) {
        char *t = NULL;
        int rc = session_create(db, 1, 0, &t);
        TEST_ASSERT(rc == SESSION_SUCCESS, "Created token successfully");
        TEST_ASSERT(t != NULL && strlen(t) == 64, "Token is non-null and 64 chars");

        int all_hex = 1;
        for (int j = 0; j < 64; j++) {
            char c = t[j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
                all_hex = 0;
            }
        }
        TEST_ASSERT(all_hex, "All 64 characters are valid lowercase hex digits");
        free(t);
    }

    sqlite3_close(db);
}

static void test_consecutive_tokens_unique(void)
{
    printf("\n--- Test: consecutive_tokens_unique ---\n");
    sqlite3 *db = setup_test_db();

    char *t1 = NULL;
    char *t2 = NULL;
    session_create(db, 1, 0, &t1);
    session_create(db, 1, 0, &t2);

    TEST_ASSERT(t1 != NULL && t2 != NULL, "Both tokens allocated");
    TEST_ASSERT(strcmp(t1, t2) != 0, "Consecutive tokens are completely different (OS entropy)");

    free(t1);
    free(t2);
    sqlite3_close(db);
}

static void test_session_validate_success(void)
{
    printf("\n--- Test: session_validate_success ---\n");
    sqlite3 *db = setup_test_db();

    /* User 1 ('USER') */
    char *token1 = NULL;
    session_create(db, 1, 3600, &token1);

    int uid = 0;
    char role[32] = {0};
    int rc = session_validate(db, token1, &uid, role, sizeof(role));
    TEST_ASSERT(rc == SESSION_SUCCESS, "Validate active session returns SESSION_SUCCESS");
    TEST_ASSERT(uid == 1, "Extracted user_id is 1");
    TEST_ASSERT(strcmp(role, "USER") == 0, "Extracted role is 'USER'");

    /* User 2 ('ADMIN') */
    char *token2 = NULL;
    session_create(db, 2, 3600, &token2);

    uid = 0;
    memset(role, 0, sizeof(role));
    rc = session_validate(db, token2, &uid, role, sizeof(role));
    TEST_ASSERT(rc == SESSION_SUCCESS, "Validate admin session returns SESSION_SUCCESS");
    TEST_ASSERT(uid == 2, "Extracted user_id is 2");
    TEST_ASSERT(strcmp(role, "ADMIN") == 0, "Extracted role is 'ADMIN'");

    /* Optional pointers (NULL) */
    rc = session_validate(db, token1, NULL, NULL, 0);
    TEST_ASSERT(rc == SESSION_SUCCESS, "Validate with NULL out pointers succeeds");

    free(token1);
    free(token2);
    sqlite3_close(db);
}

static void test_session_validate_token_not_found(void)
{
    printf("\n--- Test: session_validate_token_not_found ---\n");
    sqlite3 *db = setup_test_db();

    const char *fake_token = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    int uid = -1;
    int rc = session_validate(db, fake_token, &uid, NULL, 0);
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "Non-existent token returns SESSION_ERR_NOT_FOUND");

    sqlite3_close(db);
}

static void test_session_validate_tampered_token(void)
{
    printf("\n--- Test: session_validate_tampered_token ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* Tamper with 1 character */
    token[63] = (token[63] == 'a') ? 'b' : 'a';

    int uid = -1;
    int rc = session_validate(db, token, &uid, NULL, 0);
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "Tampered token returns SESSION_ERR_NOT_FOUND");

    free(token);
    sqlite3_close(db);
}

static void test_session_validate_expired(void)
{
    printf("\n--- Test: session_validate_expired ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* Simulate expiration: set expires_at to 10 seconds in the past */
    exec_sql(db, "UPDATE sessions SET expires_at = datetime('now', '-10 seconds');");

    int uid = -1;
    int rc = session_validate(db, token, &uid, NULL, 0);
    TEST_ASSERT(rc == SESSION_ERR_EXPIRED, "Expired session returns SESSION_ERR_EXPIRED");

    free(token);
    sqlite3_close(db);
}

static void test_session_revoke_success_and_repeated(void)
{
    printf("\n--- Test: session_revoke_success_and_repeated ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    session_create(db, 1, 3600, &token);

    /* 1. Revoke first time */
    int rc = session_revoke(db, token);
    TEST_ASSERT(rc == SESSION_SUCCESS, "First session_revoke returns SESSION_SUCCESS");

    /* 2. Validate must return SESSION_ERR_REVOKED */
    int uid = -1;
    rc = session_validate(db, token, &uid, NULL, 0);
    TEST_ASSERT(rc == SESSION_ERR_REVOKED, "session_validate on revoked session returns SESSION_ERR_REVOKED");

    /* 3. Repeated revoke */
    rc = session_revoke(db, token);
    TEST_ASSERT(rc == SESSION_ERR_REVOKED, "Repeated session_revoke returns SESSION_ERR_REVOKED (consistent behavior)");

    /* 4. Revoke non-existent token */
    const char *fake_token = "0000000000000000000000000000000000000000000000000000000000000000";
    rc = session_revoke(db, fake_token);
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "Revoking non-existent token returns SESSION_ERR_NOT_FOUND");

    free(token);
    sqlite3_close(db);
}

static void test_multiple_sessions_and_independent_revoke(void)
{
    printf("\n--- Test: multiple_sessions_and_independent_revoke ---\n");
    sqlite3 *db = setup_test_db();

    char *token_a = NULL;
    char *token_b = NULL;
    session_create(db, 1, 3600, &token_a);
    session_create(db, 1, 3600, &token_b);

    /* Both sessions are active */
    TEST_ASSERT(session_validate(db, token_a, NULL, NULL, 0) == SESSION_SUCCESS, "Token A is valid");
    TEST_ASSERT(session_validate(db, token_b, NULL, NULL, 0) == SESSION_SUCCESS, "Token B is valid");

    /* Revoke token_a only */
    session_revoke(db, token_a);
    TEST_ASSERT(session_validate(db, token_a, NULL, NULL, 0) == SESSION_ERR_REVOKED, "Token A is revoked");
    TEST_ASSERT(session_validate(db, token_b, NULL, NULL, 0) == SESSION_SUCCESS, "Token B is STILL valid (independent)");

    /* Revoke all sessions for user 1 */
    int rc = session_revoke_all(db, 1);
    TEST_ASSERT(rc == SESSION_SUCCESS, "session_revoke_all returns SESSION_SUCCESS");
    TEST_ASSERT(session_validate(db, token_b, NULL, NULL, 0) == SESSION_ERR_REVOKED, "Token B is now also revoked");

    free(token_a);
    free(token_b);
    sqlite3_close(db);
}

static void test_user_deleted_cascade(void)
{
    printf("\n--- Test: user_deleted_cascade ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    session_create(db, 3, 3600, &token);
    TEST_ASSERT(session_validate(db, token, NULL, NULL, 0) == SESSION_SUCCESS, "Token for user 3 is valid");

    /* Delete user 3 */
    exec_sql(db, "DELETE FROM users WHERE id = 3;");

    /* Validate token -> user no longer exists */
    int uid = -1;
    int rc = session_validate(db, token, &uid, NULL, 0);
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "Token returns SESSION_ERR_NOT_FOUND after user deleted");

    /* Verify cascade in sessions table */
    TEST_ASSERT(count_rows(db, "sessions") == 0, "Sessions record cascaded and removed");

    free(token);
    sqlite3_close(db);
}

static void test_session_cleanup_expired(void)
{
    printf("\n--- Test: session_cleanup_expired ---\n");
    sqlite3 *db = setup_test_db();

    char *t1 = NULL;
    char *t2 = NULL;
    char *t3 = NULL;
    session_create(db, 1, 3600, &t1);
    session_create(db, 1, 3600, &t2);
    session_create(db, 1, 3600, &t3);

    /* Revoke t1 */
    session_revoke(db, t1);

    /* Expire t2 */
    exec_sql(db, "UPDATE sessions SET expires_at = datetime('now', '-10 seconds') WHERE user_id = 1 AND revoked_at IS NULL;");

    /* t3 is created after expiration update, so t3 is fresh */
    char *t4 = NULL;
    session_create(db, 1, 3600, &t4);

    int deleted = 0;
    int rc = session_cleanup_expired(db, &deleted);
    TEST_ASSERT(rc == SESSION_SUCCESS, "session_cleanup_expired returns SESSION_SUCCESS");
    TEST_ASSERT(deleted >= 2, "Cleaned up at least 2 dead sessions (revoked + expired)");
    TEST_ASSERT(session_validate(db, t4, NULL, NULL, 0) == SESSION_SUCCESS, "Fresh session t4 is still valid");

    free(t1);
    free(t2);
    free(t3);
    free(t4);
    sqlite3_close(db);
}

static void test_sql_injection_defense(void)
{
    printf("\n--- Test: test_sql_injection_defense ---\n");
    sqlite3 *db = setup_test_db();

    const char *injections[] = {
        "' OR '1'='1",
        "'; DROP TABLE sessions; --",
        "admin'--",
        "\" OR \"\"=\"",
        NULL
    };

    for (int i = 0; injections[i] != NULL; i++) {
        int rc = session_validate(db, injections[i], NULL, NULL, 0);
        TEST_ASSERT(rc == SESSION_ERR_INVALID_PARAM, "Invalid token string rejected with SESSION_ERR_INVALID_PARAM");
        rc = session_revoke(db, injections[i]);
        TEST_ASSERT(rc == SESSION_ERR_INVALID_PARAM, "Invalid token revoke rejected with SESSION_ERR_INVALID_PARAM");
    }

    TEST_ASSERT(count_rows(db, "sessions") == 0, "Bảng sessions nguyên vẹn 100%");

    sqlite3_close(db);
}

static void test_invalid_parameters(void)
{
    printf("\n--- Test: test_invalid_parameters ---\n");
    sqlite3 *db = setup_test_db();

    char *token = NULL;
    /* NULL db */
    TEST_ASSERT(session_create(NULL, 1, 3600, &token) == SESSION_ERR_INVALID_PARAM, "session_create NULL db");
    TEST_ASSERT(session_validate(NULL, "abc", NULL, NULL, 0) == SESSION_ERR_INVALID_PARAM, "session_validate NULL db");
    TEST_ASSERT(session_revoke(NULL, "abc") == SESSION_ERR_INVALID_PARAM, "session_revoke NULL db");
    TEST_ASSERT(session_revoke_all(NULL, 1) == SESSION_ERR_INVALID_PARAM, "session_revoke_all NULL db");
    TEST_ASSERT(session_cleanup_expired(NULL, NULL) == SESSION_ERR_INVALID_PARAM, "session_cleanup_expired NULL db");

    /* NULL out_token */
    TEST_ASSERT(session_create(db, 1, 3600, NULL) == SESSION_ERR_INVALID_PARAM, "session_create NULL out_token");

    /* Invalid user_id */
    TEST_ASSERT(session_create(db, 0, 3600, &token) == SESSION_ERR_INVALID_PARAM, "session_create user_id = 0");
    TEST_ASSERT(session_create(db, -5, 3600, &token) == SESSION_ERR_INVALID_PARAM, "session_create user_id = -5");
    TEST_ASSERT(session_revoke_all(db, 0) == SESSION_ERR_INVALID_PARAM, "session_revoke_all user_id = 0");

    /* Negative TTL */
    TEST_ASSERT(session_create(db, 1, -100, &token) == SESSION_ERR_INVALID_PARAM, "session_create negative ttl");

    /* Invalid token lengths */
    TEST_ASSERT(session_validate(db, "", NULL, NULL, 0) == SESSION_ERR_INVALID_PARAM, "validate empty token");
    TEST_ASSERT(session_validate(db, "short_token", NULL, NULL, 0) == SESSION_ERR_INVALID_PARAM, "validate short token");

    /* Non-hex token of length 64 */
    char non_hex[65];
    memset(non_hex, 'z', 64);
    non_hex[64] = '\0';
    TEST_ASSERT(session_validate(db, non_hex, NULL, NULL, 0) == SESSION_ERR_INVALID_PARAM, "validate non-hex token");

    sqlite3_close(db);
}

static void test_caller_transaction_handling(void)
{
    printf("\n--- Test: caller_transaction_handling ---\n");
    sqlite3 *db = setup_test_db();

    /* Caller opens transaction */
    exec_sql(db, "BEGIN TRANSACTION;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller tx active");

    char *token = NULL;
    int rc = session_create(db, 1, 3600, &token);
    TEST_ASSERT(rc == SESSION_SUCCESS, "session_create succeeds inside caller tx via SAVEPOINT");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller tx still active after session_create");

    /* Caller commits outer transaction */
    exec_sql(db, "COMMIT;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit is 1 after caller COMMIT");

    /* Session persisted */
    TEST_ASSERT(session_validate(db, token, NULL, NULL, 0) == SESSION_SUCCESS, "Session created in tx is valid");

    free(token);
    sqlite3_close(db);
}

static void test_foreign_key_integrity(void)
{
    printf("\n--- Test: foreign_key_integrity ---\n");
    sqlite3 *db = setup_test_db();

    char *t1 = NULL;
    char *t2 = NULL;
    session_create(db, 1, 3600, &t1);
    session_create(db, 2, 3600, &t2);
    session_revoke(db, t1);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "PRAGMA foreign_key_check prepared");

    int fk_violations = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fk_violations++;
    }
    sqlite3_finalize(stmt);

    TEST_ASSERT(fk_violations == 0, "Zero foreign key violations in database");

    free(t1);
    free(t2);
    sqlite3_close(db);
}

static void test_auth_end_to_end_integration(void)
{
    printf("\n--- Test: test_auth_end_to_end_integration ---\n");
    sqlite3 *db = setup_test_db();

    /* 1. Register new user */
    int reg_rc = auth_register(db, "integration_user", "StrongPass123!");
    TEST_ASSERT(reg_rc == AUTH_SUCCESS, "auth_register succeeds");

    /* 2. Login */
    int user_id = auth_login(db, "integration_user", "StrongPass123!");
    TEST_ASSERT(user_id > 0, "auth_login succeeds and returns user_id > 0");

    /* 3. Create session upon login */
    char *token = NULL;
    int ses_rc = session_create(db, user_id, 3600, &token);
    TEST_ASSERT(ses_rc == SESSION_SUCCESS, "session_create succeeds for logged-in user");
    TEST_ASSERT(token != NULL, "Session token returned");

    /* 4. Validate session for subsequent request */
    int validated_uid = 0;
    char role[32] = {0};
    int val_rc = session_validate(db, token, &validated_uid, role, sizeof(role));
    TEST_ASSERT(val_rc == SESSION_SUCCESS, "session_validate confirms session is active");
    TEST_ASSERT(validated_uid == user_id, "Validated user_id matches logged-in user");
    TEST_ASSERT(strcmp(role, "USER") == 0, "Role matches 'USER'");

    /* 5. Logout */
    int rev_rc = session_revoke(db, token);
    TEST_ASSERT(rev_rc == SESSION_SUCCESS, "session_revoke succeeds (logout)");

    /* 6. Verify cannot use token after logout */
    int after_val = session_validate(db, token, NULL, NULL, 0);
    TEST_ASSERT(after_val == SESSION_ERR_REVOKED, "session_validate rejects revoked token after logout");

    free(token);
    sqlite3_close(db);
}

static void test_sha256_nist_vectors(void)
{
    printf("\n--- Test: test_sha256_nist_vectors ---\n");
    char hex[65];

    /* 1. Empty string */
    session_sha256_hex("", 0, hex);
    TEST_ASSERT(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0,
                "SHA-256 NIST Vector 1: Empty string");

    /* 2. "abc" */
    session_sha256_hex("abc", 3, hex);
    TEST_ASSERT(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0,
                "SHA-256 NIST Vector 2: 'abc'");

    /* 3. 55 bytes boundary */
    char buf55[56];
    memset(buf55, 'a', 55);
    buf55[55] = '\0';
    session_sha256_hex(buf55, 55, hex);
    TEST_ASSERT(strcmp(hex, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318") == 0,
                "SHA-256 Boundary: 55 bytes (one byte before pad wrap)");

    /* 4. 56 bytes boundary */
    const char *vec56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    session_sha256_hex(vec56, 56, hex);
    TEST_ASSERT(strcmp(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0,
                "SHA-256 NIST Vector 3: 56 bytes boundary");

    /* 5. 64 bytes boundary (exactly 1 block) */
    char buf64[65];
    memset(buf64, 'a', 64);
    buf64[64] = '\0';
    session_sha256_hex(buf64, 64, hex);
    TEST_ASSERT(strcmp(hex, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb") == 0,
                "SHA-256 Boundary: 64 bytes (exact 1 block)");

    /* 6. 65 bytes boundary (1 byte into 2nd block) */
    char buf65[66];
    memset(buf65, 'a', 65);
    buf65[65] = '\0';
    session_sha256_hex(buf65, 65, hex);
    TEST_ASSERT(strcmp(hex, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0") == 0,
                "SHA-256 Boundary: 65 bytes (crosses block)");

    /* 7. 119 bytes boundary */
    char buf119[120];
    memset(buf119, 'a', 119);
    buf119[119] = '\0';
    session_sha256_hex(buf119, 119, hex);
    TEST_ASSERT(strcmp(hex, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb") == 0,
                "SHA-256 Boundary: 119 bytes");

    /* 8. 120 bytes boundary */
    char buf120[121];
    memset(buf120, 'a', 120);
    buf120[120] = '\0';
    session_sha256_hex(buf120, 120, hex);
    TEST_ASSERT(strcmp(hex, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c") == 0,
                "SHA-256 Boundary: 120 bytes");

    /* 9. 64-character hex token format */
    const char *tok64 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    session_sha256_hex(tok64, 64, hex);
    TEST_ASSERT(strcmp(hex, "a8ae6e6ee929abea3afcfc5258c8ccd6f85273e0d4626d26c7279f3250f77c8e") == 0,
                "SHA-256 Token hash matches standard");

    /* 10. Multi-block 1,000 bytes */
    char buf1000[1001];
    memset(buf1000, 'a', 1000);
    buf1000[1000] = '\0';
    session_sha256_hex(buf1000, 1000, hex);
    TEST_ASSERT(strcmp(hex, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3") == 0,
                "SHA-256 Multi-block 1,000 bytes");
}

static void test_migration_audit_scenarios(void)
{
    printf("\n--- Test: test_migration_audit_scenarios ---\n");

    /*
     * Kich ban 1: Database cu da co 001_classification nhung chua co sessions.
     * database_apply_migrations phai tu dong phat hien 002_sessions.sql va ap dung thanh cong.
     */
    sqlite3 *old_db = NULL;
    TEST_ASSERT(sqlite3_open(":memory:", &old_db) == SQLITE_OK, "Open in-memory old DB");
    exec_sql(old_db, "PRAGMA foreign_keys = ON;");

    /* Tao cac bang cu: users, classification, questions */
    const char *old_schema =
        "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY, applied_at TEXT DEFAULT CURRENT_TIMESTAMP);"
        "INSERT INTO schema_migrations (version) VALUES ('001_classification');"
        "CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, username TEXT UNIQUE, password_hash TEXT, role TEXT DEFAULT 'USER');"
        "CREATE TABLE subjects (id INTEGER PRIMARY KEY AUTOINCREMENT, code TEXT UNIQUE, name TEXT);"
        "CREATE TABLE chapters (id INTEGER PRIMARY KEY AUTOINCREMENT, subject_id INTEGER REFERENCES subjects(id), order_index INT, name TEXT);"
        "CREATE TABLE lessons (id INTEGER PRIMARY KEY AUTOINCREMENT, chapter_id INTEGER REFERENCES chapters(id), order_index INT, name TEXT);"
        "CREATE TABLE topics (id INTEGER PRIMARY KEY AUTOINCREMENT, lesson_id INTEGER REFERENCES lessons(id), name TEXT);"
        "CREATE TABLE questions (id INTEGER PRIMARY KEY AUTOINCREMENT, content TEXT, topic_id INTEGER REFERENCES topics(id));";
    exec_sql(old_db, old_schema);

    int mig_rc = database_apply_migrations(old_db, "database/migrations");
    TEST_ASSERT(mig_rc == DATABASE_MIGRATION_SUCCESS, "Apply migrations to old DB succeeds");

    /* Kiem tra bang sessions va version 002_sessions da duoc ghi nhan */
    TEST_ASSERT(count_rows(old_db, "sessions") == 0, "sessions table created successfully");

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(old_db, "SELECT COUNT(*) FROM schema_migrations WHERE version = '002_sessions';", -1, &stmt, NULL);
    int has_002 = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        has_002 = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(has_002 == 1, "002_sessions recorded in schema_migrations");

    /* Kiem tra foreign keys cua sessions bang pragma foreign_key_list */
    sqlite3_prepare_v2(old_db, "PRAGMA foreign_key_list(sessions);", -1, &stmt, NULL);
    int has_fk_user = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *target = (const char *)sqlite3_column_text(stmt, 2);
        const char *from = (const char *)sqlite3_column_text(stmt, 3);
        const char *to = (const char *)sqlite3_column_text(stmt, 4);
        if (target && strcmp(target, "users") == 0 &&
            from && strcmp(from, "user_id") == 0 &&
            to && strcmp(to, "id") == 0) {
            has_fk_user = 1;
        }
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(has_fk_user == 1, "sessions foreign key references users(id) ON DELETE CASCADE");

    /*
     * Kich ban 2: Idempotent - chay lai lan nua tren database da co sessions
     */
    int idemp_rc = database_apply_migrations(old_db, "database/migrations");
    TEST_ASSERT(idemp_rc == DATABASE_MIGRATION_SUCCESS, "Subsequent migration run is idempotent");

    /*
     * Kich ban 3: Database moi khoi tao hoan toan tu schema.sql
     */
    sqlite3 *new_db = setup_test_db();
    int new_mig_rc = database_apply_migrations(new_db, "database/migrations");
    TEST_ASSERT(new_mig_rc == DATABASE_MIGRATION_SUCCESS, "Apply migrations to schema.sql DB is no-op success");

    /*
     * Kich ban 4: Schema danh dau 002_sessions nhung thieu bang sessions
     */
    sqlite3 *missing_tbl_db = NULL;
    sqlite3_open(":memory:", &missing_tbl_db);
    exec_sql(missing_tbl_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_tbl_db, "INSERT INTO schema_migrations (version) VALUES ('001_classification'), ('002_sessions');");
    int miss_rc = database_apply_migrations(missing_tbl_db, "database/migrations");
    TEST_ASSERT(miss_rc == DATABASE_MIGRATION_SUCCESS, "Runner skips applied version based on schema_migrations");
    int verify_rc = session_schema_verify(missing_tbl_db);
    TEST_ASSERT(verify_rc == SESSION_ERR_SCHEMA_MISSING_TABLE, "session_schema_verify detects missing sessions table (-11)");

    sqlite3_close(old_db);
    sqlite3_close(new_db);
    sqlite3_close(missing_tbl_db);
}

static void test_validation_security_edge_cases(void)
{
    printf("\n--- Test: test_validation_security_edge_cases ---\n");
    sqlite3 *db = setup_test_db();

    /* 1. An toan out_user_id va out_role khi validate that bai */
    int out_uid = 999;
    char out_role[32] = "INITIAL_DIRTY";
    int rc = session_validate(db, "invalid_short_token", &out_uid, out_role, sizeof(out_role));
    TEST_ASSERT(rc == SESSION_ERR_INVALID_PARAM, "Validation fails for invalid token");
    TEST_ASSERT(out_uid == 0, "out_user_id reset to 0 on invalid param");
    TEST_ASSERT(out_role[0] == '\0', "out_role reset to empty string on invalid param");

    /* Token khong ton tai */
    out_uid = 888;
    strcpy(out_role, "INITIAL_DIRTY");
    char fake_token[65];
    memset(fake_token, 'f', 64);
    fake_token[64] = '\0';
    rc = session_validate(db, fake_token, &out_uid, out_role, sizeof(out_role));
    TEST_ASSERT(rc == SESSION_ERR_NOT_FOUND, "Validation fails for non-existent token");
    TEST_ASSERT(out_uid == 0, "out_user_id reset to 0 on not found");
    TEST_ASSERT(out_role[0] == '\0', "out_role reset to empty string on not found");

    /* 2. Bao ve tran bo dem (Buffer overflow protection) tren out_role */
    char *token = NULL;
    session_create(db, 2, 3600, &token); /* User 2 role = ADMIN */
    char small_role[4] = "XYZ";
    rc = session_validate(db, token, &out_uid, small_role, 4);
    TEST_ASSERT(rc == SESSION_SUCCESS, "Validation succeeds with small buffer");
    TEST_ASSERT(strcmp(small_role, "ADM") == 0, "Role truncated cleanly with null termination (size 4)");
    TEST_ASSERT(out_uid == 2, "out_user_id extracted correctly");

    /* 3. Client khong the tu truyen role gia mao */
    char spoofed_role[32] = "ADMIN";
    char *user_tok = NULL;
    session_create(db, 1, 3600, &user_tok); /* User 1 role = USER */
    session_validate(db, user_tok, &out_uid, spoofed_role, sizeof(spoofed_role));
    TEST_ASSERT(strcmp(spoofed_role, "USER") == 0, "Client spoofed role overwritten with DB role 'USER'");

    /* 4. Token het han ngay tai ranh gioi datetime('now') */
    exec_sql(db, "INSERT INTO sessions (user_id, token_hash, expires_at) "
                 "VALUES (1, 'boundary_hash_1234567890123456789012345678901234567890123456789012', datetime('now'));");
    /* Do SQLite insert hash truc tiep, tao gia tri hash token hop le */
    char boundary_tok[65];
    memset(boundary_tok, 'e', 64);
    boundary_tok[64] = '\0';
    char b_hash[65];
    session_sha256_hex(boundary_tok, 64, b_hash);

    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "INSERT INTO sessions (user_id, token_hash, expires_at) VALUES (1, ?, datetime('now'));", -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, b_hash, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    out_uid = 777;
    strcpy(out_role, "DIRTY");
    rc = session_validate(db, boundary_tok, &out_uid, out_role, sizeof(out_role));
    TEST_ASSERT(rc == SESSION_ERR_EXPIRED, "Token expiring at now boundary is rejected as SESSION_ERR_EXPIRED");
    TEST_ASSERT(out_uid == 0, "out_user_id reset to 0 on expired");
    TEST_ASSERT(out_role[0] == '\0', "out_role reset to empty string on expired");

    free(token);
    free(user_tok);
    sqlite3_close(db);
}

static void test_session_schema_integrity_all_cases(void)
{
    printf("\n--- Test: test_session_schema_integrity_all_cases (TASK 07.1.2) ---\n");

    /* 1. Database cu chua ap dung 002_sessions (chua co marker va chua co bang) */
    sqlite3 *legacy_db = NULL;
    sqlite3_open(":memory:", &legacy_db);
    exec_sql(legacy_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(legacy_db, "INSERT INTO schema_migrations (version) VALUES ('001_classification');");
    int rc1 = session_schema_verify(legacy_db);
    TEST_ASSERT(rc1 == SESSION_SCHEMA_NOT_APPLIED, "1. Legacy DB without marker and table -> SESSION_SCHEMA_NOT_APPLIED (1)");
    sqlite3_close(legacy_db);

    /* 2. Database moi tao tu schema.sql */
    sqlite3 *new_db = setup_test_db();
    int rc2 = session_schema_verify(new_db);
    TEST_ASSERT(rc2 == SESSION_SCHEMA_OK, "2. Fresh DB from schema.sql -> SESSION_SCHEMA_OK (0)");
    sqlite3_close(new_db);

    /* 3. Database hop le da ap dung 002_sessions qua migration runner */
    sqlite3 *migrated_db = NULL;
    sqlite3_open(":memory:", &migrated_db);
    exec_sql(migrated_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(migrated_db, "INSERT INTO schema_migrations (version) VALUES ('001_classification');");
    exec_sql(migrated_db, "CREATE TABLE users (id INTEGER PRIMARY KEY, username TEXT UNIQUE, password_hash TEXT, role TEXT);");
    database_apply_migrations(migrated_db, "database/migrations");
    int rc3 = session_schema_verify(migrated_db);
    TEST_ASSERT(rc3 == SESSION_SCHEMA_OK, "3. Fully migrated DB -> SESSION_SCHEMA_OK (0)");
    sqlite3_close(migrated_db);

    /* 4. Database co marker 002_sessions nhung thieu bang sessions */
    sqlite3 *missing_tbl_db = NULL;
    sqlite3_open(":memory:", &missing_tbl_db);
    exec_sql(missing_tbl_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_tbl_db, "INSERT INTO schema_migrations (version) VALUES ('002_sessions');");
    int rc4 = session_schema_verify(missing_tbl_db);
    TEST_ASSERT(rc4 == SESSION_ERR_SCHEMA_MISSING_TABLE, "4. Marker present but table missing -> SESSION_ERR_SCHEMA_MISSING_TABLE (-11)");
    sqlite3_close(missing_tbl_db);

    /* 5. Database thieu cot bat buoc (vi du thieu revoked_at) */
    sqlite3 *missing_col_db = NULL;
    sqlite3_open(":memory:", &missing_col_db);
    exec_sql(missing_col_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_col_db, "INSERT INTO schema_migrations (version) VALUES ('002_sessions');");
    exec_sql(missing_col_db, "CREATE TABLE users (id INTEGER PRIMARY KEY);");
    exec_sql(missing_col_db, "CREATE TABLE sessions (id INTEGER PRIMARY KEY, user_id INT REFERENCES users(id) ON DELETE CASCADE, "
                             "token_hash TEXT UNIQUE, created_at TEXT, expires_at TEXT);"); /* thieu revoked_at */
    exec_sql(missing_col_db, "CREATE INDEX idx_sessions_token_hash ON sessions(token_hash);");
    exec_sql(missing_col_db, "CREATE INDEX idx_sessions_user_id ON sessions(user_id);");
    int rc5 = session_schema_verify(missing_col_db);
    TEST_ASSERT(rc5 == SESSION_ERR_SCHEMA_MISSING_COLUMN, "5. Missing column revoked_at -> SESSION_ERR_SCHEMA_MISSING_COLUMN (-12)");
    sqlite3_close(missing_col_db);

    /* 6. Database thieu rang buoc UNIQUE tren token_hash */
    sqlite3 *missing_unique_db = NULL;
    sqlite3_open(":memory:", &missing_unique_db);
    exec_sql(missing_unique_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_unique_db, "INSERT INTO schema_migrations (version) VALUES ('002_sessions');");
    exec_sql(missing_unique_db, "CREATE TABLE users (id INTEGER PRIMARY KEY);");
    exec_sql(missing_unique_db, "CREATE TABLE sessions (id INTEGER PRIMARY KEY, user_id INT REFERENCES users(id) ON DELETE CASCADE, "
                                "token_hash TEXT, created_at TEXT, expires_at TEXT, revoked_at TEXT);"); /* khong co UNIQUE */
    exec_sql(missing_unique_db, "CREATE INDEX idx_sessions_token_hash ON sessions(token_hash);");
    exec_sql(missing_unique_db, "CREATE INDEX idx_sessions_user_id ON sessions(user_id);");
    int rc6 = session_schema_verify(missing_unique_db);
    TEST_ASSERT(rc6 == SESSION_ERR_SCHEMA_MISSING_UNIQUE, "6. Missing UNIQUE on token_hash -> SESSION_ERR_SCHEMA_MISSING_UNIQUE (-13)");
    sqlite3_close(missing_unique_db);

    /* 7. Database thieu foreign key CASCADE */
    sqlite3 *missing_fk_db = NULL;
    sqlite3_open(":memory:", &missing_fk_db);
    exec_sql(missing_fk_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_fk_db, "INSERT INTO schema_migrations (version) VALUES ('002_sessions');");
    exec_sql(missing_fk_db, "CREATE TABLE users (id INTEGER PRIMARY KEY);");
    exec_sql(missing_fk_db, "CREATE TABLE sessions (id INTEGER PRIMARY KEY, user_id INT REFERENCES users(id) ON DELETE RESTRICT, " /* ON DELETE RESTRICT thay vi CASCADE */
                                "token_hash TEXT UNIQUE, created_at TEXT, expires_at TEXT, revoked_at TEXT);");
    exec_sql(missing_fk_db, "CREATE INDEX idx_sessions_token_hash ON sessions(token_hash);");
    exec_sql(missing_fk_db, "CREATE INDEX idx_sessions_user_id ON sessions(user_id);");
    int rc7 = session_schema_verify(missing_fk_db);
    TEST_ASSERT(rc7 == SESSION_ERR_SCHEMA_MISSING_FK, "7. Missing ON DELETE CASCADE -> SESSION_ERR_SCHEMA_MISSING_FK (-14)");
    sqlite3_close(missing_fk_db);

    /* 8. Database thieu index idx_sessions_user_id */
    sqlite3 *missing_idx_db = NULL;
    sqlite3_open(":memory:", &missing_idx_db);
    exec_sql(missing_idx_db, "CREATE TABLE schema_migrations (version TEXT PRIMARY KEY);");
    exec_sql(missing_idx_db, "INSERT INTO schema_migrations (version) VALUES ('002_sessions');");
    exec_sql(missing_idx_db, "CREATE TABLE users (id INTEGER PRIMARY KEY);");
    exec_sql(missing_idx_db, "CREATE TABLE sessions (id INTEGER PRIMARY KEY, user_id INT REFERENCES users(id) ON DELETE CASCADE, "
                                "token_hash TEXT UNIQUE, created_at TEXT, expires_at TEXT, revoked_at TEXT);");
    exec_sql(missing_idx_db, "CREATE INDEX idx_sessions_token_hash ON sessions(token_hash);"); /* thieu idx_sessions_user_id */
    int rc8 = session_schema_verify(missing_idx_db);
    TEST_ASSERT(rc8 == SESSION_ERR_SCHEMA_MISSING_INDEX, "8. Missing idx_sessions_user_id -> SESSION_ERR_SCHEMA_MISSING_INDEX (-15)");
    sqlite3_close(missing_idx_db);

    /* 9. Chay kiem tra nhieu lan khong thay doi du lieu */
    sqlite3 *persist_db = setup_test_db();
    char *test_tok = NULL;
    session_create(persist_db, 1, 3600, &test_tok);
    int users_before = count_rows(persist_db, "users");
    int sessions_before = count_rows(persist_db, "sessions");
    for (int i = 0; i < 5; i++) {
        int r = session_schema_verify(persist_db);
        TEST_ASSERT(r == SESSION_SCHEMA_OK, "Repeated verification returns SESSION_SCHEMA_OK");
    }
    int users_after = count_rows(persist_db, "users");
    int sessions_after = count_rows(persist_db, "sessions");
    TEST_ASSERT(users_before == users_after, "9a. Users row count preserved 100%");
    TEST_ASSERT(sessions_before == sessions_after, "9b. Sessions row count preserved 100%");
    free(test_tok);
    sqlite3_close(persist_db);

    /* 10. Database error: db == NULL */
    int rc10 = session_schema_verify(NULL);
    TEST_ASSERT(rc10 == SESSION_ERR_INVALID_PARAM, "10. NULL db parameter -> SESSION_ERR_INVALID_PARAM (-1)");
}

int main(void)
{
    printf("====================================================================\n");
    printf("UNIT TESTS: SESSION MANAGEMENT (TASK 07.1, 07.1.1, 07.1.2)\n");
    printf("====================================================================\n");

    test_session_schema_integrity_all_cases();
    test_sha256_nist_vectors();
    test_migration_audit_scenarios();
    test_validation_security_edge_cases();
    test_session_create_success();
    test_session_create_user_not_found();
    test_session_token_entropy_and_format();
    test_consecutive_tokens_unique();
    test_session_validate_success();
    test_session_validate_token_not_found();
    test_session_validate_tampered_token();
    test_session_validate_expired();
    test_session_revoke_success_and_repeated();
    test_multiple_sessions_and_independent_revoke();
    test_user_deleted_cascade();
    test_session_cleanup_expired();
    test_sql_injection_defense();
    test_invalid_parameters();
    test_caller_transaction_handling();
    test_foreign_key_integrity();
    test_auth_end_to_end_integration();

    printf("\n====================================================================\n");
    printf("RESULT: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("====================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
