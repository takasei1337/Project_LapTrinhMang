#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua Migration Runner
 * ============================================================================
 */
#define DATABASE_MIGRATION_SUCCESS               0  /* Migration thanh cong hoac da up-to-date */
#define DATABASE_MIGRATION_ERR_PARAM            -1  /* Tham so db hoac migrations_dir NULL/rong */
#define DATABASE_MIGRATION_ERR_DIR              -2  /* Thu muc migrations_dir khong ton tai hoac khong the doc */
#define DATABASE_MIGRATION_ERR_DATABASE         -3  /* Loi truy van SQLite */
#define DATABASE_MIGRATION_ERR_TRANSACTION      -4  /* Caller dang mo transaction dở dang, tu choi migration */
#define DATABASE_MIGRATION_ERR_FAILED           -5  /* Migration script loi, da rollback an toan */
#define DATABASE_MIGRATION_ERR_MEMORY           -6  /* Loi cap phat bo nho */
#define DATABASE_MIGRATION_ERR_INCOMPATIBLE     -7  /* Schema khong tuong thich hoac vi pham khoa ngoai */

/*
 * Mo ket noi den database SQLite va bat PRAGMA foreign_keys = ON.
 * Neu bien moi truong MIGRATIONS_DIR duoc thiet lap, ham se tu dong ap dung
 * migration. Neu migration that bai, ham dong ket noi va tra ve NULL.
 *
 * Tra ve con tro sqlite3 neu thanh cong, NULL neu that bai.
 */
sqlite3 *database_open(const char *db_path);

/*
 * Dong ket noi database.
 */
void database_close(sqlite3 *db);

/*
 * Ap dung tat ca cac file migration (.sql) trong migrations_dir theo thu tu phien ban.
 * - Tu dong tao bang schema_migrations neu chua co.
 * - Bo qua cac migration da ghi nhan.
 * - Xu ly an toan rollback neu co loi, khong de ho transaction (autocommit luon = 1).
 * - Bao ve transaction cua caller (khong tu y rollback neu caller dang co transaction).
 * - Ho tro dieu hoa baseline khi DB da mang du schema cua phien ban ma chua co log.
 * - Khong tu dong danh dau thanh cong neu schema chua day du hoac loi.
 *
 * Tra ve:
 * - DATABASE_MIGRATION_SUCCESS (0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi DATABASE_MIGRATION_ERR_* khi that bai.
 */
int database_apply_migrations(sqlite3 *db, const char *migrations_dir);

/*
 * Mo ket noi database va tu dong ap dung migration tu migrations_dir.
 * Neu mo DB that bai hoac bat ky migration nao that bai, ham dong DB va tra ve NULL.
 */
sqlite3 *database_open_with_migrations(const char *db_path, const char *migrations_dir);

#endif /* DATABASE_H */
