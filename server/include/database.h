
#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>

/*
 * Mo ket noi den database SQLite.
 *
 * Tra ve con tro sqlite3 neu thanh cong.
 * Tra ve NULL neu that bai.
 */
sqlite3 *database_open(const char *db_path);

/*
 * Dong ket noi database.
 */
void database_close(sqlite3 *db);

#endif
