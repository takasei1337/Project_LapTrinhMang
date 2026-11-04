#include "logger.h"

#include <stddef.h>
#include <string.h>

int logger_log(sqlite3 *db, int user_id, const char *action)
{
    if (db == NULL || action == NULL || action[0] == '\0') {
        return -1;
    }

    const char *sql = "INSERT INTO activity_logs (user_id, action) VALUES (?, ?);";
    sqlite3_stmt *stmt = NULL;

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return -1;
    }

    if (user_id > 0) {
        rc = sqlite3_bind_int(stmt, 1, user_id);
    } else {
        rc = sqlite3_bind_null(stmt, 1);
    }

    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }

    rc = sqlite3_bind_text(stmt, 2, action, -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        return -1;
    }

    int step_rc = sqlite3_step(stmt);
    int finalize_rc = sqlite3_finalize(stmt);

    if (step_rc != SQLITE_DONE || finalize_rc != SQLITE_OK) {
        return -1;
    }

    return 0;
}

int logger_log_activity(sqlite3 *db, int user_id, const char *action)
{
    return logger_log(db, user_id, action);
}
