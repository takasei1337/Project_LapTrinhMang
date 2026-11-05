#include "session.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sqlite3.h>

/*
 * ============================================================================
 * SHA-256 Self-Contained Implementation (FIPS 180-4)
 * ============================================================================
 */

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} SHA256_CTX;

#define ROR32(val, bits) (((val) >> (bits)) | ((val) << (32 - (bits))))
#define CH(x, y, z)      (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z)     (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)           (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x)           (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x)          (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x)          (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static const uint32_t K256[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static void sha256_transform(uint32_t state[8], const uint8_t data[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               ((uint32_t)data[i * 4 + 3]);
    }
    for (int i = 16; i < 64; i++) {
        w[i] = SIG1(w[i - 2]) + w[i - 7] + SIG0(w[i - 15]) + w[i - 16];
    }

    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e, f, g) + K256[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

static void sha256_init(SHA256_CTX *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x6a09e667U;
    ctx->state[1] = 0xbb67ae85U;
    ctx->state[2] = 0x3c6ef372U;
    ctx->state[3] = 0xa54ff53aU;
    ctx->state[4] = 0x510e527fU;
    ctx->state[5] = 0x9b05688cU;
    ctx->state[6] = 0x1f83d9abU;
    ctx->state[7] = 0x5be0cd19U;
}

static void sha256_update(SHA256_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t index = (size_t)((ctx->count >> 3) & 0x3F);
    ctx->count += ((uint64_t)len << 3);
    size_t part_len = 64 - index;
    size_t i = 0;

    if (len >= part_len) {
        memcpy(&ctx->buffer[index], data, part_len);
        sha256_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64) {
            sha256_transform(ctx->state, &data[i]);
        }
        index = 0;
    }
    memcpy(&ctx->buffer[index], &data[i], len - i);
}

static void sha256_final(SHA256_CTX *ctx, uint8_t digest[32])
{
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    for (int i = 0; i < 8; i++) {
        bits[i] = (uint8_t)((ctx->count >> ((7 - i) * 8)) & 0xFF);
    }
    size_t index = (size_t)((ctx->count >> 3) & 0x3F);
    size_t pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha256_update(ctx, padding, pad_len);
    sha256_update(ctx, bits, 8);

    for (int i = 0; i < 8; i++) {
        digest[i * 4]     = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFF);
    }
}

static void secure_clean_memory(void *v, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)v;
    while (n--) {
        *p++ = 0;
    }
}

void session_sha256_hex(const void *data, size_t len, char hex_out[65])
{
    if (hex_out == NULL) {
        return;
    }
    hex_out[0] = '\0';
    if (data == NULL && len > 0) {
        return;
    }

    uint8_t digest[32];
    SHA256_CTX ctx;
    sha256_init(&ctx);
    if (len > 0) {
        sha256_update(&ctx, (const uint8_t *)data, len);
    }
    sha256_final(&ctx, digest);

    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        hex_out[i * 2]     = hex_chars[(digest[i] >> 4) & 0x0F];
        hex_out[i * 2 + 1] = hex_chars[digest[i] & 0x0F];
    }
    hex_out[64] = '\0';
    secure_clean_memory(digest, sizeof(digest));
    secure_clean_memory(&ctx, sizeof(ctx));
}

static void compute_sha256_hex(const char *input, char hex_out[65])
{
    session_sha256_hex(input, input ? strlen(input) : 0, hex_out);
}

/*
 * ============================================================================
 * Helper Functions
 * ============================================================================
 */

static int generate_entropy(uint8_t *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t ret = getrandom(buf + total, len - total, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        total += (size_t)ret;
    }
    if (total == len) {
        return 0;
    }

    /* Fallback sang /dev/urandom neu getrandom() khong kha dung */
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        return -1;
    }
    size_t rb = 0;
    while (rb < len) {
        size_t n = fread(buf + rb, 1, len - rb, f);
        if (n == 0) {
            if (ferror(f) && errno == EINTR) {
                clearerr(f);
                continue;
            }
            fclose(f);
            return -1;
        }
        rb += n;
    }
    fclose(f);
    return 0;
}

static int is_valid_token_format(const char *token)
{
    if (token == NULL) {
        return 0;
    }
    size_t len = strlen(token);
    if (len != SESSION_TOKEN_HEX_LEN) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        char c = token[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static int check_user_exists(sqlite3 *db, int user_id)
{
    const char *sql = "SELECT id FROM users WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, user_id);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step == SQLITE_ROW) {
        return 1;
    }
    if (step == SQLITE_DONE) {
        return 0;
    }
    return -1;
}

static int session_tx_begin(sqlite3 *db, int caller_has_tx)
{
    if (!caller_has_tx) {
        return sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);
    } else {
        return sqlite3_exec(db, "SAVEPOINT session_sp;", NULL, NULL, NULL);
    }
}

static void session_tx_rollback(sqlite3 *db, int caller_has_tx)
{
    if (!caller_has_tx) {
        sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
    } else {
        sqlite3_exec(db, "ROLLBACK TO session_sp;", NULL, NULL, NULL);
        sqlite3_exec(db, "RELEASE session_sp;", NULL, NULL, NULL);
    }
}

static int session_tx_commit(sqlite3 *db, int caller_has_tx)
{
    if (!caller_has_tx) {
        return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
    } else {
        return sqlite3_exec(db, "RELEASE session_sp;", NULL, NULL, NULL);
    }
}

/*
 * ============================================================================
 * Session Management APIs
 * ============================================================================
 */

int session_create(sqlite3 *db,
                   int user_id,
                   int ttl_seconds,
                   char **out_token)
{
    if (out_token == NULL) {
        return SESSION_ERR_INVALID_PARAM;
    }
    *out_token = NULL;

    if (db == NULL || user_id <= 0) {
        return SESSION_ERR_INVALID_PARAM;
    }

    if (ttl_seconds < 0) {
        return SESSION_ERR_INVALID_PARAM;
    }

    if (ttl_seconds == 0) {
        ttl_seconds = SESSION_DEFAULT_TTL_SECONDS;
    }

    /* 1. Kiem tra user ton tai va hop le */
    int user_check = check_user_exists(db, user_id);
    if (user_check == 0) {
        return SESSION_ERR_NOT_FOUND;
    }
    if (user_check < 0) {
        return SESSION_ERR_DATABASE;
    }

    /* 2. Sinh ngau nhien 32 byte entropy (256-bit entropy) */
    uint8_t raw_token[32];
    if (generate_entropy(raw_token, sizeof(raw_token)) != 0) {
        secure_clean_memory(raw_token, sizeof(raw_token));
        return SESSION_ERR_GENERATE_TOKEN;
    }

    char hex_token[SESSION_TOKEN_HEX_LEN + 1];
    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < sizeof(raw_token); i++) {
        hex_token[i * 2]     = hex_chars[(raw_token[i] >> 4) & 0x0F];
        hex_token[i * 2 + 1] = hex_chars[raw_token[i] & 0x0F];
    }
    hex_token[SESSION_TOKEN_HEX_LEN] = '\0';
    secure_clean_memory(raw_token, sizeof(raw_token));

    /* 3. Tinh ma bam SHA-256 cua token de luu vao DB */
    char token_hash[65];
    compute_sha256_hex(hex_token, token_hash);

    /* 4. Luu token_hash vao bang sessions voi transaction an toan */
    int caller_has_tx = (sqlite3_get_autocommit(db) == 0);
    if (session_tx_begin(db, caller_has_tx) != SQLITE_OK) {
        secure_clean_memory(hex_token, sizeof(hex_token));
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    const char *sql_insert =
        "INSERT INTO sessions (user_id, token_hash, expires_at) "
        "VALUES (?, ?, datetime('now', '+' || ? || ' seconds'));";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql_insert, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        session_tx_rollback(db, caller_has_tx);
        secure_clean_memory(hex_token, sizeof(hex_token));
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, token_hash, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, ttl_seconds);

    int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    secure_clean_memory(token_hash, sizeof(token_hash));

    if (step_rc != SQLITE_DONE) {
        session_tx_rollback(db, caller_has_tx);
        secure_clean_memory(hex_token, sizeof(hex_token));
        return SESSION_ERR_DATABASE;
    }

    if (session_tx_commit(db, caller_has_tx) != SQLITE_OK) {
        session_tx_rollback(db, caller_has_tx);
        secure_clean_memory(hex_token, sizeof(hex_token));
        return SESSION_ERR_DATABASE;
    }

    /* 5. Cap phat chuoi token tra ve cho caller */
    char *token_out = (char *)malloc(SESSION_TOKEN_HEX_LEN + 1);
    if (token_out == NULL) {
        secure_clean_memory(hex_token, sizeof(hex_token));
        return SESSION_ERR_MEMORY;
    }
    memcpy(token_out, hex_token, SESSION_TOKEN_HEX_LEN + 1);
    secure_clean_memory(hex_token, sizeof(hex_token));

    *out_token = token_out;
    return SESSION_SUCCESS;
}

int session_validate(sqlite3 *db,
                     const char *token,
                     int *out_user_id,
                     char *out_role,
                     size_t role_size)
{
    /* Thiet lap trang thai an toan cho cac con tro xuat ngay tu dau */
    if (out_user_id != NULL) {
        *out_user_id = 0;
    }
    if (out_role != NULL && role_size > 0) {
        out_role[0] = '\0';
    }

    if (db == NULL || !is_valid_token_format(token)) {
        return SESSION_ERR_INVALID_PARAM;
    }

    /* 1. Tinh ma bam SHA-256 cua token truyen vao */
    char token_hash[65];
    compute_sha256_hex(token, token_hash);

    /* 2. Truy van thong tin phien kem trang thai het han va vai tro nguoi dung */
    const char *sql =
        "SELECT s.id, s.user_id, s.expires_at, s.revoked_at, u.role, "
        "       (datetime('now') >= datetime(s.expires_at)) AS is_expired "
        "FROM sessions s "
        "JOIN users u ON s.user_id = u.id "
        "WHERE s.token_hash = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    sqlite3_bind_text(stmt, 1, token_hash, -1, SQLITE_TRANSIENT);
    secure_clean_memory(token_hash, sizeof(token_hash));

    int step_rc = sqlite3_step(stmt);
    if (step_rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return SESSION_ERR_NOT_FOUND;
    }

    if (step_rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return SESSION_ERR_DATABASE;
    }

    int user_id = sqlite3_column_int(stmt, 1);
    int is_revoked = (sqlite3_column_type(stmt, 3) != SQLITE_NULL);
    const char *role_text = (const char *)sqlite3_column_text(stmt, 4);
    int is_expired = sqlite3_column_int(stmt, 5);

    if (is_revoked) {
        sqlite3_finalize(stmt);
        return SESSION_ERR_REVOKED;
    }

    if (is_expired) {
        sqlite3_finalize(stmt);
        return SESSION_ERR_EXPIRED;
    }

    if (out_user_id != NULL) {
        *out_user_id = user_id;
    }

    if (out_role != NULL && role_size > 0) {
        if (role_text != NULL) {
            snprintf(out_role, role_size, "%s", role_text);
        } else {
            out_role[0] = '\0';
        }
    }

    sqlite3_finalize(stmt);
    return SESSION_SUCCESS;
}

int session_revoke(sqlite3 *db, const char *token)
{
    if (db == NULL || !is_valid_token_format(token)) {
        return SESSION_ERR_INVALID_PARAM;
    }

    char token_hash[65];
    compute_sha256_hex(token, token_hash);

    /* 1. Kiem tra phien co ton tai khong va trang thai revoked_at */
    const char *sql_check = "SELECT id, revoked_at FROM sessions WHERE token_hash = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql_check, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    sqlite3_bind_text(stmt, 1, token_hash, -1, SQLITE_TRANSIENT);
    int step_rc = sqlite3_step(stmt);

    if (step_rc == SQLITE_DONE) {
        sqlite3_finalize(stmt);
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_NOT_FOUND;
    }

    if (step_rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    int already_revoked = (sqlite3_column_type(stmt, 1) != SQLITE_NULL);
    sqlite3_finalize(stmt);

    /* Hanh vi nhat quan: Neu da logout truoc do, tra ve SESSION_ERR_REVOKED */
    if (already_revoked) {
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_REVOKED;
    }

    /* 2. Cap nhat revoked_at */
    const char *sql_update =
        "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP "
        "WHERE token_hash = ? AND revoked_at IS NULL;";

    sqlite3_stmt *stmt_up = NULL;
    rc = sqlite3_prepare_v2(db, sql_update, -1, &stmt_up, NULL);
    if (rc != SQLITE_OK || stmt_up == NULL) {
        secure_clean_memory(token_hash, sizeof(token_hash));
        return SESSION_ERR_DATABASE;
    }

    sqlite3_bind_text(stmt_up, 1, token_hash, -1, SQLITE_TRANSIENT);
    secure_clean_memory(token_hash, sizeof(token_hash));

    int step_up = sqlite3_step(stmt_up);
    sqlite3_finalize(stmt_up);

    if (step_up != SQLITE_DONE) {
        return SESSION_ERR_DATABASE;
    }

    return SESSION_SUCCESS;
}

int session_revoke_all(sqlite3 *db, int user_id)
{
    if (db == NULL || user_id <= 0) {
        return SESSION_ERR_INVALID_PARAM;
    }

    /* Kiem tra user ton tai */
    int user_check = check_user_exists(db, user_id);
    if (user_check == 0) {
        return SESSION_ERR_NOT_FOUND;
    }
    if (user_check < 0) {
        return SESSION_ERR_DATABASE;
    }

    const char *sql =
        "UPDATE sessions SET revoked_at = CURRENT_TIMESTAMP "
        "WHERE user_id = ? AND revoked_at IS NULL;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return SESSION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step != SQLITE_DONE) {
        return SESSION_ERR_DATABASE;
    }

    return SESSION_SUCCESS;
}

int session_cleanup_expired(sqlite3 *db, int *out_deleted_count)
{
    if (out_deleted_count != NULL) {
        *out_deleted_count = 0;
    }

    if (db == NULL) {
        return SESSION_ERR_INVALID_PARAM;
    }

    const char *sql =
        "DELETE FROM sessions "
        "WHERE datetime('now') >= datetime(expires_at) OR revoked_at IS NOT NULL;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return SESSION_ERR_DATABASE;
    }

    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step != SQLITE_DONE) {
        return SESSION_ERR_DATABASE;
    }

    if (out_deleted_count != NULL) {
        *out_deleted_count = sqlite3_changes(db);
    }

    return SESSION_SUCCESS;
}

/*
 * ============================================================================
 * Schema Integrity Verification (Task 07.1.2)
 * ============================================================================
 */

static int check_table_exists(sqlite3 *db, const char *table_name)
{
    const char *sql = "SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int check_migration_marker_exists(sqlite3 *db, const char *version)
{
    if (check_table_exists(db, "schema_migrations") != 1) {
        return 0;
    }
    const char *sql = "SELECT COUNT(*) FROM schema_migrations WHERE version = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, version, -1, SQLITE_STATIC);
    int has = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        has = (sqlite3_column_int(stmt, 0) > 0);
    }
    sqlite3_finalize(stmt);
    return has;
}

static int check_sessions_columns(sqlite3 *db)
{
    /* Bat buoc: id, user_id, token_hash, created_at, expires_at, revoked_at */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA table_info(sessions);", -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    int has_id = 0, has_user_id = 0, has_token_hash = 0;
    int has_created_at = 0, has_expires_at = 0, has_revoked_at = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *col = (const char *)sqlite3_column_text(stmt, 1);
        if (!col) continue;
        if (strcasecmp(col, "id") == 0) has_id = 1;
        else if (strcasecmp(col, "user_id") == 0) has_user_id = 1;
        else if (strcasecmp(col, "token_hash") == 0) has_token_hash = 1;
        else if (strcasecmp(col, "created_at") == 0) has_created_at = 1;
        else if (strcasecmp(col, "expires_at") == 0) has_expires_at = 1;
        else if (strcasecmp(col, "revoked_at") == 0) has_revoked_at = 1;
    }
    sqlite3_finalize(stmt);

    return (has_id && has_user_id && has_token_hash &&
            has_created_at && has_expires_at && has_revoked_at) ? 1 : 0;
}

static int check_token_hash_unique(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA index_list(sessions);", -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    int has_unique = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *idx_name = (const char *)sqlite3_column_text(stmt, 1);
        int is_unique = sqlite3_column_int(stmt, 2);
        if (is_unique && idx_name) {
            char sql_info[256];
            snprintf(sql_info, sizeof(sql_info), "PRAGMA index_info(%s);", idx_name);
            sqlite3_stmt *stmt_info = NULL;
            if (sqlite3_prepare_v2(db, sql_info, -1, &stmt_info, NULL) == SQLITE_OK && stmt_info) {
                while (sqlite3_step(stmt_info) == SQLITE_ROW) {
                    const char *indexed_col = (const char *)sqlite3_column_text(stmt_info, 2);
                    if (indexed_col && strcasecmp(indexed_col, "token_hash") == 0) {
                        has_unique = 1;
                        break;
                    }
                }
                sqlite3_finalize(stmt_info);
            }
        }
        if (has_unique) break;
    }
    sqlite3_finalize(stmt);
    return has_unique;
}

static int check_sessions_foreign_key(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_list(sessions);", -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    int fk_valid = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *target_tbl = (const char *)sqlite3_column_text(stmt, 2);
        const char *from_col = (const char *)sqlite3_column_text(stmt, 3);
        const char *to_col = (const char *)sqlite3_column_text(stmt, 4);
        const char *on_del = (const char *)sqlite3_column_text(stmt, 6);

        if (target_tbl && strcasecmp(target_tbl, "users") == 0 &&
            from_col && strcasecmp(from_col, "user_id") == 0 &&
            to_col && strcasecmp(to_col, "id") == 0 &&
            on_del && strcasecmp(on_del, "CASCADE") == 0) {
            fk_valid = 1;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return fk_valid;
}

static int check_sessions_indexes(sqlite3 *db)
{
    /* Kiem tra 2 index bat buoc: idx_sessions_token_hash va idx_sessions_user_id */
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA index_list(sessions);", -1, &stmt, NULL) != SQLITE_OK || !stmt) {
        return -1;
    }
    int has_idx_hash = 0, has_idx_uid = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *idx_name = (const char *)sqlite3_column_text(stmt, 1);
        if (!idx_name) continue;
        if (strcasecmp(idx_name, "idx_sessions_token_hash") == 0) {
            has_idx_hash = 1;
        } else if (strcasecmp(idx_name, "idx_sessions_user_id") == 0) {
            has_idx_uid = 1;
        }
    }
    sqlite3_finalize(stmt);
    return (has_idx_hash && has_idx_uid) ? 1 : 0;
}

int session_schema_verify(sqlite3 *db)
{
    if (db == NULL) {
        return SESSION_ERR_INVALID_PARAM;
    }

    int has_marker = check_migration_marker_exists(db, "002_sessions");
    if (has_marker < 0) {
        return SESSION_ERR_DATABASE;
    }

    int has_table = check_table_exists(db, "sessions");
    if (has_table < 0) {
        return SESSION_ERR_DATABASE;
    }

    /* Neu chua co marker va chua co bang -> Migration chua ap dung */
    if (!has_marker && !has_table) {
        return SESSION_SCHEMA_NOT_APPLIED;
    }

    /* Neu da co marker nhung thieu bang sessions -> Schema bi thieu nghiem trong */
    if (has_marker && !has_table) {
        return SESSION_ERR_SCHEMA_MISSING_TABLE;
    }

    /* Kiem tra day du 6 cot bat buoc */
    int cols_ok = check_sessions_columns(db);
    if (cols_ok < 0) {
        return SESSION_ERR_DATABASE;
    }
    if (!cols_ok) {
        return SESSION_ERR_SCHEMA_MISSING_COLUMN;
    }

    /* Kiem tra rang buoc UNIQUE tren cot token_hash */
    int unique_ok = check_token_hash_unique(db);
    if (unique_ok < 0) {
        return SESSION_ERR_DATABASE;
    }
    if (!unique_ok) {
        return SESSION_ERR_SCHEMA_MISSING_UNIQUE;
    }

    /* Kiem tra rang buoc Foreign Key sessions.user_id -> users(id) ON DELETE CASCADE */
    int fk_ok = check_sessions_foreign_key(db);
    if (fk_ok < 0) {
        return SESSION_ERR_DATABASE;
    }
    if (!fk_ok) {
        return SESSION_ERR_SCHEMA_MISSING_FK;
    }

    /* Kiem tra su ton tai cua 2 index hieu nang */
    int idx_ok = check_sessions_indexes(db);
    if (idx_ok < 0) {
        return SESSION_ERR_DATABASE;
    }
    if (!idx_ok) {
        return SESSION_ERR_SCHEMA_MISSING_INDEX;
    }

    /* Neu schema day du nhung chua co marker 002_sessions trong schema_migrations -> khong dong bo */
    if (!has_marker) {
        return SESSION_ERR_SCHEMA_CORRUPT;
    }

    return SESSION_SCHEMA_OK;
}
