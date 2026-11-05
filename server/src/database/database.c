#include "database.h"

#include <stdio.h>
#include <stdlib.h>

static sqlite3 *database_open_internal(const char *db_path)
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

sqlite3 *database_open(const char *db_path)
{
    sqlite3 *db = database_open_internal(db_path);
    if (db == NULL) {
        return NULL;
    }

    /*
     * Xu ly Migration:
     * - Truong hop 1: Bien moi truong MIGRATIONS_DIR duoc thiet lap.
     *   Migration la bat buoc. Neu migration that bai, dong ket noi va tra ve NULL.
     * - Truong hop 2: Khong thiet lap MIGRATIONS_DIR.
     *   Khoi phuc hanh vi tuong thich nguoc: chi mo database va bat PRAGMA foreign_keys = ON,
     *   khong tu dong chay migration va tuyet doi khong tu dong fallback sang "database/migrations".
     */
    const char *env_mig_dir = getenv("MIGRATIONS_DIR");
    if (env_mig_dir != NULL) {
        int mig_rc = database_apply_migrations(db, env_mig_dir);
        if (mig_rc != DATABASE_MIGRATION_SUCCESS) {
            fprintf(stderr, "Database error: Explicit MIGRATIONS_DIR migration failed with code %d\n", mig_rc);
            database_close(db);
            return NULL;
        }
    }

    return db;
}

sqlite3 *database_open_with_migrations(const char *db_path, const char *migrations_dir)
{
    if (migrations_dir == NULL || migrations_dir[0] == '\0') {
        fprintf(stderr, "Database error: migrations_dir cannot be NULL or empty in database_open_with_migrations\n");
        return NULL;
    }

    sqlite3 *db = database_open_internal(db_path);
    if (db == NULL) {
        return NULL;
    }

    int rc = database_apply_migrations(db, migrations_dir);
    if (rc != DATABASE_MIGRATION_SUCCESS) {
        fprintf(stderr, "Database error: Mandatory migration failed with code %d\n", rc);
        database_close(db);
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
