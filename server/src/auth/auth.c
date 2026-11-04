#include "auth.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <sys/random.h>
#include <argon2.h>
#include <sqlite3.h>

#define ARGON2_T_COST       2
#define ARGON2_M_COST       65536
#define ARGON2_PARALLELISM  1
#define ARGON2_SALT_LEN     16
#define ARGON2_HASH_LEN     32
#define ARGON2_ENCODED_LEN  128

static void secure_clean_memory(void *v, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)v;
    while (n--) {
        *p++ = 0;
    }
}

static int generate_salt(uint8_t *salt, size_t salt_len)
{
    size_t total = 0;
    while (total < salt_len) {
        ssize_t ret = getrandom(salt + total, salt_len - total, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        total += (size_t)ret;
    }

    if (total == salt_len) {
        return 0;
    }

    /* Fallback sang /dev/urandom neu getrandom gap loi */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        return -1;
    }
    size_t read_bytes = fread(salt, 1, salt_len, f);
    fclose(f);
    return (read_bytes == salt_len) ? 0 : -1;
}

static int is_valid_username(const char *username)
{
    if (username == NULL) {
        return 0;
    }

    size_t len = strlen(username);
    if (len < 3 || len > 32) {
        return 0;
    }

    for (size_t i = 0; i < len; i++) {
        char c = username[i];
        if (!((c >= 'a' && c <= 'z') ||
              (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') ||
              (c == '_'))) {
            return 0;
        }
    }

    return 1;
}

static int is_valid_password(const char *password)
{
    if (password == NULL) {
        return 0;
    }

    size_t len = strlen(password);
    if (len < 8 || len > 128) {
        return 0;
    }

    return 1;
}

int auth_register(sqlite3 *db, const char *username, const char *password)
{
    /* 1. Kiem tra tham so dau vao */
    if (db == NULL || !is_valid_username(username) || !is_valid_password(password)) {
        return AUTH_ERR_INVALID_PARAM;
    }

    /* 2. Sinh salt ngau nhien an toan tu he dieu hanh */
    uint8_t salt[ARGON2_SALT_LEN];
    if (generate_salt(salt, sizeof(salt)) != 0) {
        return AUTH_ERR_HASH_FAILED;
    }

    /* 3. Bam mat khau bang Argon2id (version 19, format encoded $argon2id$...) */
    char encoded[ARGON2_ENCODED_LEN];
    int hash_rc = argon2id_hash_encoded(
        ARGON2_T_COST,
        ARGON2_M_COST,
        ARGON2_PARALLELISM,
        password,
        strlen(password),
        salt,
        sizeof(salt),
        ARGON2_HASH_LEN,
        encoded,
        sizeof(encoded)
    );

    /* Xoa salt khoi bo nho sau khi bam xong */
    secure_clean_memory(salt, sizeof(salt));

    if (hash_rc != ARGON2_OK) {
        secure_clean_memory(encoded, sizeof(encoded));
        return AUTH_ERR_HASH_FAILED;
    }

    /* 4. Luu tai khoan vao SQLite bang prepared statement */
    const char *sql = "INSERT INTO users (username, password_hash) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        secure_clean_memory(encoded, sizeof(encoded));
        return AUTH_ERR_DATABASE;
    }

    rc = sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        secure_clean_memory(encoded, sizeof(encoded));
        return AUTH_ERR_DATABASE;
    }

    rc = sqlite3_bind_text(stmt, 2, encoded, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        secure_clean_memory(encoded, sizeof(encoded));
        return AUTH_ERR_DATABASE;
    }

    int step_rc = sqlite3_step(stmt);
    int ext_rc = (step_rc != SQLITE_DONE) ? sqlite3_extended_errcode(db) : SQLITE_OK;
    int fin_rc = sqlite3_finalize(stmt);
    secure_clean_memory(encoded, sizeof(encoded));

    if (step_rc == SQLITE_DONE && fin_rc == SQLITE_OK) {
        return AUTH_SUCCESS;
    }

    if (ext_rc == SQLITE_CONSTRAINT_UNIQUE) {
        return AUTH_ERR_USER_EXISTS;
    }

    return AUTH_ERR_DATABASE;
}

int auth_login(sqlite3 *db, const char *username, const char *password)
{
    /* 1. Kiem tra tham so dau vao */
    if (db == NULL || !is_valid_username(username) || !is_valid_password(password)) {
        return AUTH_ERR_INVALID_PARAM;
    }

    /* 2. Chuan bi cau truy van lay thong tin tai khoan */
    const char *sql = "SELECT id, password_hash FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return AUTH_ERR_DATABASE;
    }

    rc = sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return AUTH_ERR_DATABASE;
    }

    int step_rc = sqlite3_step(stmt);

    if (step_rc == SQLITE_DONE) {
        /* Khong tim thay username trong CSDL */
        sqlite3_finalize(stmt);
        return AUTH_ERR_INVALID_CREDENTIALS;
    }

    if (step_rc != SQLITE_ROW) {
        /* Loi truy van database */
        sqlite3_finalize(stmt);
        return AUTH_ERR_DATABASE;
    }

    /* 3. Doc id va password_hash */
    sqlite3_int64 raw_id = sqlite3_column_int64(stmt, 0);
    const unsigned char *hash_text = sqlite3_column_text(stmt, 1);
    int hash_bytes = sqlite3_column_bytes(stmt, 1);

    if (raw_id <= 0 || raw_id > INT_MAX) {
        sqlite3_finalize(stmt);
        return AUTH_ERR_DATABASE;
    }

    int user_id = (int)raw_id;

    /* Kiem tra tinh hop le cua chuoi hash (khong NULL, khong rong, khong vuot qua 255 byte) */
    if (hash_text == NULL || hash_bytes <= 0 || hash_bytes > 255) {
        sqlite3_finalize(stmt);
        return AUTH_ERR_HASH_FAILED;
    }

    /* Sao chep hash sang bo dem an toan truoc khi finalize statement */
    char hash_copy[256];
    memcpy(hash_copy, hash_text, (size_t)hash_bytes);
    hash_copy[hash_bytes] = '\0';

    int fin_rc = sqlite3_finalize(stmt);
    if (fin_rc != SQLITE_OK) {
        secure_clean_memory(hash_copy, sizeof(hash_copy));
        return AUTH_ERR_DATABASE;
    }

    /* 4. Xac minh mat khau voi Argon2id */
    int verify_rc = argon2id_verify(hash_copy, password, strlen(password));
    secure_clean_memory(hash_copy, sizeof(hash_copy));

    if (verify_rc == ARGON2_OK) {
        return user_id;
    }

    if (verify_rc == ARGON2_VERIFY_MISMATCH) {
        return AUTH_ERR_INVALID_CREDENTIALS;
    }

    return AUTH_ERR_HASH_FAILED;
}

