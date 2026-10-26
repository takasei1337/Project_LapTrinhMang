
#include "database.h"

#include <stdio.h>

sqlite3 *database_open(const char *db_path)
{
    sqlite3 *db = NULL;

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "Database error: %s\n",
                sqlite3_errmsg(db));

        sqlite3_close(db);
        return NULL;
    }

    /* Bat kiem tra khoa ngoai cho moi ket noi SQLite */
    char *error_message = NULL;

    if (sqlite3_exec(db, "PRAGMA foreign_keys = ON;",
                     NULL, NULL, &error_message) != SQLITE_OK) {
        fprintf(stderr, "Database configuration error: %s\n",
                error_message != NULL ? error_message : "Unknown error");

        sqlite3_free(error_message);
        sqlite3_close(db);
        return NULL;
    }

    return db;
}

void database_close(sqlite3 *db)
{
    if (db != NULL) {
        sqlite3_close(db);
    }
}
