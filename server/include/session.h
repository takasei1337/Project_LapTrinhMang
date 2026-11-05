#ifndef SESSION_H
#define SESSION_H

#include <sqlite3.h>
#include <stddef.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Session Management
 * ============================================================================
 */
#define SESSION_SUCCESS               0  /* Thao tac thanh cong */
#define SESSION_ERR_INVALID_PARAM   -1  /* Tham so dau vao khong hop le hoac con tro NULL */
#define SESSION_ERR_DATABASE        -2  /* Loi truy van, prepare hoac ghi du lieu SQLite */
#define SESSION_ERR_NOT_FOUND       -3  /* Nguoi dung hoac phien khong ton tai */
#define SESSION_ERR_EXPIRED         -4  /* Phien dang nhap da het han */
#define SESSION_ERR_REVOKED         -5  /* Phien dang nhap da bi thu hoi / dang xuat */
#define SESSION_ERR_MEMORY          -6  /* Loi cap phat bo nho dong */
#define SESSION_ERR_GENERATE_TOKEN  -7  /* Loi sinh entropy ngau nhien tu he dieu hanh */

/*
 * Ma trang thai kiem tra tinh toan ven schema Session (Task 07.1.2)
 */
#define SESSION_SCHEMA_OK                   0  /* Schema day du, hop le va da san sang hoat dong */
#define SESSION_SCHEMA_NOT_APPLIED          1  /* Migration 002_sessions chua duoc ap dung */
#define SESSION_ERR_SCHEMA_CORRUPT        -10  /* Da ghi nhan 002_sessions nhung schema bi hong/khong nhat quan */
#define SESSION_ERR_SCHEMA_MISSING_TABLE   -11 /* Thieu bang sessions */
#define SESSION_ERR_SCHEMA_MISSING_COLUMN  -12 /* Thieu mot hoac nhieu cot bat buoc */
#define SESSION_ERR_SCHEMA_MISSING_UNIQUE  -13 /* Thieu rang buoc UNIQUE tren cot token_hash */
#define SESSION_ERR_SCHEMA_MISSING_FK      -14 /* Thieu Foreign Key sessions.user_id -> users(id) ON DELETE CASCADE */
#define SESSION_ERR_SCHEMA_MISSING_INDEX   -15 /* Thieu index bat buoc tren token_hash hoac user_id */

/*
 * ============================================================================
 * Hang so cau hinh
 * ============================================================================
 */
#define SESSION_TOKEN_HEX_LEN       64     /* 32 bytes entropy = 256 bits -> 64 ky tu hex */
#define SESSION_DEFAULT_TTL_SECONDS 86400  /* Thoi gian song mac dinh: 24 gio */

/*
 * ============================================================================
 * API: session_create
 * ============================================================================
 *
 * Tao mot phien dang nhap moi cho nguoi dung da xac thuc:
 * - Kiem tra user_id hop le va ton tai trong bang users.
 * - Sinh ngau nhien 32 byte entropy bang getrandom() (fallback /dev/urandom).
 * - Chuyen 32 byte entropy thanh chuoi hex 64 ky tu (khong dung rand/timestamp/user_id).
 * - Tinh ma bam SHA-256 cua token va luu vao cot token_hash cua bang sessions.
 *   (Tuyet doi khong luu plaintext token trong CSDL va khong ghi token vao log).
 * - Luu thoi gian het han expires_at = CURRENT_TIMESTAMP + ttl_seconds.
 * - Ho tro nhieu phien dang nhap doc lap tren mot tai khoan (Multi-session).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - user_id: ID nguoi dung hop le (> 0).
 * - ttl_seconds: Thoi gian song cua phien tinh bang giay (> 0).
 *                Neu <= 0, tu dong su dung SESSION_DEFAULT_TTL_SECONDS (86400s).
 * - out_token: Con tro nhan chuoi token plaintext duoc cap phat dong.
 *
 * Hop dong bo nho:
 * - Khi thanh cong: *out_token tro toi chuoi hex 64 ky tu (+ '\0').
 *   Caller so huu chuoi nay va phai goi free() sau khi su dung.
 * - Khi that bai: Neu out_token != NULL thi *out_token duoc gan ve NULL.
 *
 * Tra ve:
 * - SESSION_SUCCESS (0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi SESSION_ERR_* khi that bai.
 */
int session_create(sqlite3 *db,
                   int user_id,
                   int ttl_seconds,
                   char **out_token);

/*
 * ============================================================================
 * API: session_validate
 * ============================================================================
 *
 * Xac minh tinh hop le cua token phien:
 * - Kiem tra token khong NULL va dung dinh dang 64 ky tu hex.
 * - Tinh ma bam SHA-256 cua token va truy van bang sessions (JOIN users).
 * - Kiem tra trang thai thu hoi (revoked_at IS NOT NULL) -> SESSION_ERR_REVOKED.
 * - Kiem tra thoi han (CURRENT_TIMESTAMP > expires_at) -> SESSION_ERR_EXPIRED.
 * - Kiem tra nguoi dung con ton tai trong he thong -> SESSION_ERR_NOT_FOUND.
 * - Lay thong tin user_id va role thuc te cua tai khoan de cung cap cho Router.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - token: Chuoi token 64 ky tu hex can xac minh.
 * - out_user_id: Con tro nhan user_id (co the NULL neu khong can lay).
 * - out_role: Buffer nhan vai tro (vi du "USER", "ADMIN", co the NULL).
 * - role_size: Kich thuoc buffer out_role.
 *
 * Tra ve:
 * - SESSION_SUCCESS (0) neu phien hop le, con han va chua bi thu hoi.
 * - SESSION_ERR_INVALID_PARAM (-1) neu tham so khong hop le hoac sai dinh dang token.
 * - SESSION_ERR_NOT_FOUND (-3) neu token khong ton tai hoac user bi xoa.
 * - SESSION_ERR_EXPIRED (-4) neu phien da het han.
 * - SESSION_ERR_REVOKED (-5) neu phien da bi dang xuat / thu hoi.
 * - SESSION_ERR_DATABASE (-2) neu loi SQLite.
 */
int session_validate(sqlite3 *db,
                     const char *token,
                     int *out_user_id,
                     char *out_role,
                     size_t role_size);

/*
 * ============================================================================
 * API: session_revoke
 * ============================================================================
 *
 * Thu hoi mot phien dang nhap cu the (Dang xuat don phien / Logout):
 * - Tinh ma bam SHA-256 cua token va tim kiem session.
 * - Neu session khong ton tai: tra ve SESSION_ERR_NOT_FOUND (-3).
 * - Neu session da bi thu hoi truoc do (revoked_at IS NOT NULL):
 *   tra ve SESSION_ERR_REVOKED (-5) de bao hieu hanh vi nhat quan la phien da logout.
 * - Neu session dang hoat dong: cap nhat revoked_at = CURRENT_TIMESTAMP.
 *   Token nay khong the tai su dung. Cac phien khac cua user khong bi anh huong.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - token: Chuoi token can thu hoi.
 *
 * Tra ve:
 * - SESSION_SUCCESS (0) khi thu hoi thanh cong lan dau.
 * - SESSION_ERR_REVOKED (-5) khi thu hoi lap lai (session da logout truoc do).
 * - SESSION_ERR_NOT_FOUND (-3) khi token khong ton tai.
 * - Ma am khac khi gap loi.
 */
int session_revoke(sqlite3 *db, const char *token);

/*
 * ============================================================================
 * API: session_revoke_all
 * ============================================================================
 *
 * Thu hoi tat ca cac phien dang hoat dong cua mot nguoi dung:
 * - Phuc vu cac tinh huong doi mat khau, khoa tai khoan hoac dang xuat moi noi.
 * - Kiem tra user_id ton tai trong bang users.
 * - Cap nhat revoked_at = CURRENT_TIMESTAMP cho toan bo session dang active cua user.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - user_id: ID nguoi dung (> 0).
 *
 * Tra ve:
 * - SESSION_SUCCESS (0) khi thanh cong.
 * - SESSION_ERR_NOT_FOUND (-3) neu user_id khong ton tai.
 * - Ma am khac khi gap loi.
 */
int session_revoke_all(sqlite3 *db, int user_id);

/*
 * ============================================================================
 * API: session_cleanup_expired
 * ============================================================================
 *
 * Don dep dinh ky cac phien da het han hoac da bi thu hoi:
 * - Xoa cac ban ghi trong sessions co datetime('now') > datetime(expires_at)
 *   hoac revoked_at IS NOT NULL.
 * - Tra ve so luong ban ghi da xoa qua *out_deleted_count.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - out_deleted_count: Con tro nhan so luong ban ghi da xoa (co the NULL).
 *
 * Tra ve:
 * - SESSION_SUCCESS (0) khi thanh cong.
 * - Ma am khi gap loi SQLite.
 */
int session_cleanup_expired(sqlite3 *db, int *out_deleted_count);

/*
 * ============================================================================
 * Helper API: session_sha256_hex
 * ============================================================================
 *
 * Tinh ma bam SHA-256 (FIPS 180-4) cho du lieu bat ky va xuat ra chuoi hex 64 ky tu.
 * Phuc vu xac minh doc lap theo bo NIST test vectors va su dung noi bo.
 *
 * Tham so:
 * - data: Con tro toi du lieu dau vao (co the NULL neu len == 0).
 * - len: Do dai du lieu dau vao tinh bang byte.
 * - hex_out: Buffer it nhat 65 bytes nhan chuoi hex 64 ky tu kem ky tu ket thuc '\0'.
 */
void session_sha256_hex(const void *data, size_t len, char hex_out[65]);

/*
 * ============================================================================
 * API: session_schema_verify (Task 07.1.2)
 * ============================================================================
 *
 * Kiem tra tinh toan ven schema Session trong CSDL (co the tai su dung luc server startup):
 * - Kiem tra trang thai ghi nhan cua migration 002_sessions trong bang schema_migrations.
 * - Xac minh su ton tai cua bang sessions.
 * - Xac minh day du 6 cot bat buoc: id, user_id, token_hash, created_at, expires_at, revoked_at.
 * - Xac minh rang buoc UNIQUE tren cot token_hash.
 * - Xac minh rang buoc Foreign Key sessions.user_id -> users(id) voi ON DELETE CASCADE.
 * - Xac minh su ton tai cua cac index hieu nang: idx_sessions_token_hash va idx_sessions_user_id.
 *
 * Tra ve:
 * - SESSION_SCHEMA_OK (0): Schema hop le va da migrate day du.
 * - SESSION_SCHEMA_NOT_APPLIED (1): Migration 002_sessions chua duoc ap dung (marker va bang chua co).
 * - SESSION_ERR_INVALID_PARAM (-1): Tham so db la NULL.
 * - SESSION_ERR_SCHEMA_MISSING_TABLE (-11): Marker ton tai nhung thieu bang sessions.
 * - SESSION_ERR_SCHEMA_MISSING_COLUMN (-12): Thieu cot bat buoc trong bang sessions.
 * - SESSION_ERR_SCHEMA_MISSING_UNIQUE (-13): Thieu rang buoc UNIQUE tren cot token_hash.
 * - SESSION_ERR_SCHEMA_MISSING_FK (-14): Thieu rang buoc Foreign Key hoac sai che do ON DELETE CASCADE.
 * - SESSION_ERR_SCHEMA_MISSING_INDEX (-15): Thieu index bat buoc tren token_hash hoac user_id.
 * - SESSION_ERR_SCHEMA_CORRUPT (-10): Schema o trang thai khong dong bo / bi hong.
 * - SESSION_ERR_DATABASE (-2): Gap loi khi truy van SQLite.
 */
int session_schema_verify(sqlite3 *db);

#endif /* SESSION_H */

