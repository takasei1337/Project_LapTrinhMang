#ifndef LOGGER_H
#define LOGGER_H

#include <sqlite3.h>

/*
 * Ghi mot su kien hoat dong vao bang activity_logs.
 *
 * Tham so:
 * - db: Con tro ket noi CSDL SQLite3 (khong duoc NULL).
 * - user_id: ID cua nguoi dung (> 0). Neu user_id <= 0, gia tri user_id trong CSDL
 *            se duoc luu la NULL (danh cho he thong hoac khi chua xac dinh nguoi dung).
 * - action: Chuoi mo ta hanh dong (khong duoc NULL hoac chuoi rong).
 *
 * Tra ve:
 * - 0 neu thanh cong.
 * - -1 neu that bai (tham so khong hop le, loi SQL hoac vi pham rang buoc khoa ngoai).
 */
int logger_log(sqlite3 *db, int user_id, const char *action);

/*
 * Alias cho logger_log.
 */
int logger_log_activity(sqlite3 *db, int user_id, const char *action);

#endif /* LOGGER_H */

