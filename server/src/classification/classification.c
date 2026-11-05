#include "classification.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/*
 * ============================================================================
 * Helper Functions
 * ============================================================================
 */

static int is_blank_string(const char *str)
{
    if (str == NULL) {
        return 1;
    }
    while (*str != '\0') {
        if (!isspace((unsigned char)*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

static int map_sqlite_error(sqlite3 *db, int default_err)
{
    int errcode = sqlite3_extended_errcode(db);
    int primary = errcode & 0xff;
    if (primary == SQLITE_CONSTRAINT) {
        if (errcode == SQLITE_CONSTRAINT_UNIQUE) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
        if (errcode == SQLITE_CONSTRAINT_FOREIGNKEY || errcode == SQLITE_CONSTRAINT_TRIGGER) {
            return CLASSIFICATION_ERR_IN_USE;
        }
        if (errcode == SQLITE_CONSTRAINT_CHECK) {
            return CLASSIFICATION_ERR_INVALID_PARAM;
        }
        const char *msg = sqlite3_errmsg(db);
        if (msg) {
            if (strstr(msg, "UNIQUE")) return CLASSIFICATION_ERR_DUPLICATE;
            if (strstr(msg, "FOREIGN KEY")) return CLASSIFICATION_ERR_IN_USE;
            if (strstr(msg, "CHECK")) return CLASSIFICATION_ERR_INVALID_PARAM;
        }
    }
    return default_err;
}

static int subject_exists(sqlite3 *db, int id)
{
    const char *sql = "SELECT 1 FROM subjects WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int chapter_exists(sqlite3 *db, int id)
{
    const char *sql = "SELECT 1 FROM chapters WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int lesson_exists(sqlite3 *db, int id)
{
    const char *sql = "SELECT 1 FROM lessons WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int topic_exists(sqlite3 *db, int id)
{
    const char *sql = "SELECT 1 FROM topics WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

/*
 * ============================================================================
 * SUBJECT IMPLEMENTATION
 * ============================================================================
 */

int classification_subject_create(sqlite3 *db,
                                  const char *code,
                                  const char *name,
                                  const char *description,
                                  int *out_subject_id)
{
    if (out_subject_id != NULL) {
        *out_subject_id = 0;
    }
    if (db == NULL || code == NULL || name == NULL || out_subject_id == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (is_blank_string(code) || is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    /* Kiem tra trung ma mon hoc */
    const char *chk_sql = "SELECT 1 FROM subjects WHERE code = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, chk_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (exists) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    const char *sql = "INSERT INTO subjects (code, name, description) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_text(stmt, 1, code, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    if (description != NULL) {
        sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    *out_subject_id = (int)sqlite3_last_insert_rowid(db);
    return CLASSIFICATION_SUCCESS;
}

int classification_subject_list(sqlite3 *db,
                                int limit,
                                int offset,
                                char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    /* Dem tong so mon hoc */
    const char *count_sql = "SELECT COUNT(*) FROM subjects;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    const char *sel_sql =
        "SELECT s.id, s.code, s.name, s.description, s.created_at, "
        "       (SELECT COUNT(*) FROM chapters c WHERE c.subject_id = s.id) AS chapter_count "
        "FROM subjects s ORDER BY s.id ASC LIMIT ? OFFSET ?;";
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "subjects");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddStringToObject(item, "code", (const char *)sqlite3_column_text(stmt, 1));
        cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 2));
        const char *desc = (const char *)sqlite3_column_text(stmt, 3);
        if (desc != NULL) {
            cJSON_AddStringToObject(item, "description", desc);
        } else {
            cJSON_AddNullToObject(item, "description");
        }
        const char *cat = (const char *)sqlite3_column_text(stmt, 4);
        cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
        cJSON_AddNumberToObject(item, "chapter_count", sqlite3_column_int(stmt, 5));

        cJSON_AddItemToArray(items, item);
        count++;
    }
    sqlite3_finalize(stmt);

    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON_AddNumberToObject(root, "offset", offset);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_subject_get(sqlite3 *db,
                               int subject_id,
                               char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || subject_id <= 0 || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql =
        "SELECT s.id, s.code, s.name, s.description, s.created_at, "
        "       (SELECT COUNT(*) FROM chapters c WHERE c.subject_id = s.id) AS chapter_count "
        "FROM subjects s WHERE s.id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
    cJSON_AddStringToObject(item, "code", (const char *)sqlite3_column_text(stmt, 1));
    cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 2));
    const char *desc = (const char *)sqlite3_column_text(stmt, 3);
    if (desc != NULL) {
        cJSON_AddStringToObject(item, "description", desc);
    } else {
        cJSON_AddNullToObject(item, "description");
    }
    const char *cat = (const char *)sqlite3_column_text(stmt, 4);
    cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
    cJSON_AddNumberToObject(item, "chapter_count", sqlite3_column_int(stmt, 5));
    sqlite3_finalize(stmt);

    char *json_out = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_subject_update(sqlite3 *db,
                                  int subject_id,
                                  const char *code,
                                  const char *name,
                                  const char *description)
{
    if (db == NULL || subject_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    /* Phai cung cap it nhat mot truong de cap nhat */
    if (code == NULL && name == NULL && description == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (code != NULL && is_blank_string(code)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (name != NULL && is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    /* Kiem tra ton tai va doc gia tri cu */
    const char *sel_sql = "SELECT code, name, description FROM subjects WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    char old_code[256];
    char old_name[256];
    char old_desc[1024];
    int has_old_desc = 0;

    const char *cur_code = (const char *)sqlite3_column_text(stmt, 0);
    const char *cur_name = (const char *)sqlite3_column_text(stmt, 1);
    const char *cur_desc = (const char *)sqlite3_column_text(stmt, 2);
    snprintf(old_code, sizeof(old_code), "%s", cur_code ? cur_code : "");
    snprintf(old_name, sizeof(old_name), "%s", cur_name ? cur_name : "");
    if (cur_desc != NULL) {
        has_old_desc = 1;
        snprintf(old_desc, sizeof(old_desc), "%s", cur_desc);
    }
    sqlite3_finalize(stmt);

    const char *final_code = (code != NULL) ? code : old_code;
    const char *final_name = (name != NULL) ? name : old_name;
    const char *final_desc = (description != NULL) ? description : (has_old_desc ? old_desc : NULL);

    /* Kiem tra trung ma code voi subject khac */
    if (code != NULL && strcmp(final_code, old_code) != 0) {
        const char *dup_sql = "SELECT 1 FROM subjects WHERE code = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_text(stmt, 1, final_code, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, subject_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    const char *upd_sql = "UPDATE subjects SET code = ?, name = ?, description = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, upd_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_text(stmt, 1, final_code, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
    if (final_desc != NULL) {
        sqlite3_bind_text(stmt, 3, final_desc, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, subject_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

int classification_subject_delete(sqlite3 *db,
                                  int subject_id)
{
    if (db == NULL || subject_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int ex = subject_exists(db, subject_id);
    if (ex == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (ex < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra rang buoc RESTRICT: chuong con dang ton tai */
    const char *child_sql = "SELECT 1 FROM chapters WHERE subject_id = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, child_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    int in_use = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (in_use) {
        return CLASSIFICATION_ERR_IN_USE;
    }

    const char *del_sql = "DELETE FROM subjects WHERE id = ?;";
    if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * CHAPTER IMPLEMENTATION
 * ============================================================================
 */

int classification_chapter_create(sqlite3 *db,
                                  int subject_id,
                                  int order_index,
                                  const char *name,
                                  const char *description,
                                  int *out_chapter_id)
{
    if (out_chapter_id != NULL) {
        *out_chapter_id = 0;
    }
    if (db == NULL || subject_id <= 0 || order_index <= 0 ||
        name == NULL || out_chapter_id == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    /* Kiem tra mon hoc cha ton tai */
    int parent = subject_exists(db, subject_id);
    if (parent == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (parent < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra trung order_index trong cung subject */
    const char *chk_order_sql = "SELECT 1 FROM chapters WHERE subject_id = ? AND order_index = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, chk_order_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    sqlite3_bind_int(stmt, 2, order_index);
    int order_dup = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (order_dup) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    /* Kiem tra trung ten chuong trong cung subject */
    const char *chk_name_sql = "SELECT 1 FROM chapters WHERE subject_id = ? AND name = ?;";
    if (sqlite3_prepare_v2(db, chk_name_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    int name_dup = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (name_dup) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    const char *sql = "INSERT INTO chapters (subject_id, order_index, name, description) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, subject_id);
    sqlite3_bind_int(stmt, 2, order_index);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    if (description != NULL) {
        sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    *out_chapter_id = (int)sqlite3_last_insert_rowid(db);
    return CLASSIFICATION_SUCCESS;
}

int classification_chapter_list(sqlite3 *db,
                                int subject_id,
                                int limit,
                                int offset,
                                char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    /* Dem tong so chuong */
    const char *count_sql_filter = "SELECT COUNT(*) FROM chapters WHERE subject_id = ?;";
    const char *count_sql_all = "SELECT COUNT(*) FROM chapters;";
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    if (subject_id > 0) {
        rc = sqlite3_prepare_v2(db, count_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) sqlite3_bind_int(stmt, 1, subject_id);
    } else {
        rc = sqlite3_prepare_v2(db, count_sql_all, -1, &stmt, NULL);
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    const char *sel_sql_filter =
        "SELECT c.id, c.subject_id, c.order_index, c.name, c.description, c.created_at, "
        "       (SELECT COUNT(*) FROM lessons l WHERE l.chapter_id = c.id) AS lesson_count "
        "FROM chapters c WHERE c.subject_id = ? "
        "ORDER BY c.order_index ASC, c.id ASC LIMIT ? OFFSET ?;";
    const char *sel_sql_all =
        "SELECT c.id, c.subject_id, c.order_index, c.name, c.description, c.created_at, "
        "       (SELECT COUNT(*) FROM lessons l WHERE l.chapter_id = c.id) AS lesson_count "
        "FROM chapters c "
        "ORDER BY c.subject_id ASC, c.order_index ASC, c.id ASC LIMIT ? OFFSET ?;";

    if (subject_id > 0) {
        rc = sqlite3_prepare_v2(db, sel_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, subject_id);
            sqlite3_bind_int(stmt, 2, limit);
            sqlite3_bind_int(stmt, 3, offset);
        }
    } else {
        rc = sqlite3_prepare_v2(db, sel_sql_all, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            sqlite3_bind_int(stmt, 2, offset);
        }
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "chapters");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddNumberToObject(item, "subject_id", sqlite3_column_int(stmt, 1));
        cJSON_AddNumberToObject(item, "order_index", sqlite3_column_int(stmt, 2));
        cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 3));
        const char *desc = (const char *)sqlite3_column_text(stmt, 4);
        if (desc != NULL) {
            cJSON_AddStringToObject(item, "description", desc);
        } else {
            cJSON_AddNullToObject(item, "description");
        }
        const char *cat = (const char *)sqlite3_column_text(stmt, 5);
        cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
        cJSON_AddNumberToObject(item, "lesson_count", sqlite3_column_int(stmt, 6));

        cJSON_AddItemToArray(items, item);
        count++;
    }
    sqlite3_finalize(stmt);

    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON_AddNumberToObject(root, "offset", offset);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_chapter_get(sqlite3 *db,
                               int chapter_id,
                               char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || chapter_id <= 0 || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql =
        "SELECT c.id, c.subject_id, c.order_index, c.name, c.description, c.created_at, "
        "       (SELECT COUNT(*) FROM lessons l WHERE l.chapter_id = c.id) AS lesson_count "
        "FROM chapters c WHERE c.id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
    cJSON_AddNumberToObject(item, "subject_id", sqlite3_column_int(stmt, 1));
    cJSON_AddNumberToObject(item, "order_index", sqlite3_column_int(stmt, 2));
    cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 3));
    const char *desc = (const char *)sqlite3_column_text(stmt, 4);
    if (desc != NULL) {
        cJSON_AddStringToObject(item, "description", desc);
    } else {
        cJSON_AddNullToObject(item, "description");
    }
    const char *cat = (const char *)sqlite3_column_text(stmt, 5);
    cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
    cJSON_AddNumberToObject(item, "lesson_count", sqlite3_column_int(stmt, 6));
    sqlite3_finalize(stmt);

    char *json_out = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_chapter_update(sqlite3 *db,
                                  int chapter_id,
                                  int order_index,
                                  const char *name,
                                  const char *description)
{
    if (db == NULL || chapter_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (order_index <= 0 && name == NULL && description == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (name != NULL && is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql = "SELECT subject_id, order_index, name, description FROM chapters WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    int subject_id = sqlite3_column_int(stmt, 0);
    int old_order = sqlite3_column_int(stmt, 1);
    char old_name[256];
    char old_desc[1024];
    int has_old_desc = 0;

    const char *cur_name = (const char *)sqlite3_column_text(stmt, 2);
    const char *cur_desc = (const char *)sqlite3_column_text(stmt, 3);
    snprintf(old_name, sizeof(old_name), "%s", cur_name ? cur_name : "");
    if (cur_desc != NULL) {
        has_old_desc = 1;
        snprintf(old_desc, sizeof(old_desc), "%s", cur_desc);
    }
    sqlite3_finalize(stmt);

    int final_order = (order_index > 0) ? order_index : old_order;
    const char *final_name = (name != NULL) ? name : old_name;
    const char *final_desc = (description != NULL) ? description : (has_old_desc ? old_desc : NULL);

    /* Kiem tra trung order_index trong cung subject */
    if (order_index > 0 && final_order != old_order) {
        const char *dup_sql = "SELECT 1 FROM chapters WHERE subject_id = ? AND order_index = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_int(stmt, 1, subject_id);
        sqlite3_bind_int(stmt, 2, final_order);
        sqlite3_bind_int(stmt, 3, chapter_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    /* Kiem tra trung ten trong cung subject */
    if (name != NULL && strcmp(final_name, old_name) != 0) {
        const char *dup_sql = "SELECT 1 FROM chapters WHERE subject_id = ? AND name = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_int(stmt, 1, subject_id);
        sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, chapter_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    const char *upd_sql = "UPDATE chapters SET order_index = ?, name = ?, description = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, upd_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, final_order);
    sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
    if (final_desc != NULL) {
        sqlite3_bind_text(stmt, 3, final_desc, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, chapter_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

int classification_chapter_delete(sqlite3 *db,
                                  int chapter_id)
{
    if (db == NULL || chapter_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int ex = chapter_exists(db, chapter_id);
    if (ex == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (ex < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra rang buoc RESTRICT: bai hoc con dang ton tai */
    const char *child_sql = "SELECT 1 FROM lessons WHERE chapter_id = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, child_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    int in_use = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (in_use) {
        return CLASSIFICATION_ERR_IN_USE;
    }

    const char *del_sql = "DELETE FROM chapters WHERE id = ?;";
    if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * LESSON IMPLEMENTATION
 * ============================================================================
 */

int classification_lesson_create(sqlite3 *db,
                                 int chapter_id,
                                 int order_index,
                                 const char *name,
                                 const char *description,
                                 int *out_lesson_id)
{
    if (out_lesson_id != NULL) {
        *out_lesson_id = 0;
    }
    if (db == NULL || chapter_id <= 0 || order_index <= 0 ||
        name == NULL || out_lesson_id == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    /* Kiem tra chuong cha ton tai */
    int parent = chapter_exists(db, chapter_id);
    if (parent == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (parent < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra trung order_index trong cung chapter */
    const char *chk_order_sql = "SELECT 1 FROM lessons WHERE chapter_id = ? AND order_index = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, chk_order_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    sqlite3_bind_int(stmt, 2, order_index);
    int order_dup = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (order_dup) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    /* Kiem tra trung ten bai hoc trong cung chapter */
    const char *chk_name_sql = "SELECT 1 FROM lessons WHERE chapter_id = ? AND name = ?;";
    if (sqlite3_prepare_v2(db, chk_name_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    int name_dup = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (name_dup) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    const char *sql = "INSERT INTO lessons (chapter_id, order_index, name, description) VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, chapter_id);
    sqlite3_bind_int(stmt, 2, order_index);
    sqlite3_bind_text(stmt, 3, name, -1, SQLITE_STATIC);
    if (description != NULL) {
        sqlite3_bind_text(stmt, 4, description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 4);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    *out_lesson_id = (int)sqlite3_last_insert_rowid(db);
    return CLASSIFICATION_SUCCESS;
}

int classification_lesson_list(sqlite3 *db,
                               int chapter_id,
                               int limit,
                               int offset,
                               char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    const char *count_sql_filter = "SELECT COUNT(*) FROM lessons WHERE chapter_id = ?;";
    const char *count_sql_all = "SELECT COUNT(*) FROM lessons;";
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    if (chapter_id > 0) {
        rc = sqlite3_prepare_v2(db, count_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) sqlite3_bind_int(stmt, 1, chapter_id);
    } else {
        rc = sqlite3_prepare_v2(db, count_sql_all, -1, &stmt, NULL);
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    const char *sel_sql_filter =
        "SELECT l.id, l.chapter_id, l.order_index, l.name, l.description, l.created_at, "
        "       (SELECT COUNT(*) FROM topics t WHERE t.lesson_id = l.id) AS topic_count "
        "FROM lessons l WHERE l.chapter_id = ? "
        "ORDER BY l.order_index ASC, l.id ASC LIMIT ? OFFSET ?;";
    const char *sel_sql_all =
        "SELECT l.id, l.chapter_id, l.order_index, l.name, l.description, l.created_at, "
        "       (SELECT COUNT(*) FROM topics t WHERE t.lesson_id = l.id) AS topic_count "
        "FROM lessons l "
        "ORDER BY l.chapter_id ASC, l.order_index ASC, l.id ASC LIMIT ? OFFSET ?;";

    if (chapter_id > 0) {
        rc = sqlite3_prepare_v2(db, sel_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, chapter_id);
            sqlite3_bind_int(stmt, 2, limit);
            sqlite3_bind_int(stmt, 3, offset);
        }
    } else {
        rc = sqlite3_prepare_v2(db, sel_sql_all, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            sqlite3_bind_int(stmt, 2, offset);
        }
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "lessons");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddNumberToObject(item, "chapter_id", sqlite3_column_int(stmt, 1));
        cJSON_AddNumberToObject(item, "order_index", sqlite3_column_int(stmt, 2));
        cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 3));
        const char *desc = (const char *)sqlite3_column_text(stmt, 4);
        if (desc != NULL) {
            cJSON_AddStringToObject(item, "description", desc);
        } else {
            cJSON_AddNullToObject(item, "description");
        }
        const char *cat = (const char *)sqlite3_column_text(stmt, 5);
        cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
        cJSON_AddNumberToObject(item, "topic_count", sqlite3_column_int(stmt, 6));

        cJSON_AddItemToArray(items, item);
        count++;
    }
    sqlite3_finalize(stmt);

    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON_AddNumberToObject(root, "offset", offset);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_lesson_get(sqlite3 *db,
                              int lesson_id,
                              char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || lesson_id <= 0 || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql =
        "SELECT l.id, l.chapter_id, l.order_index, l.name, l.description, l.created_at, "
        "       (SELECT COUNT(*) FROM topics t WHERE t.lesson_id = l.id) AS topic_count "
        "FROM lessons l WHERE l.id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
    cJSON_AddNumberToObject(item, "chapter_id", sqlite3_column_int(stmt, 1));
    cJSON_AddNumberToObject(item, "order_index", sqlite3_column_int(stmt, 2));
    cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 3));
    const char *desc = (const char *)sqlite3_column_text(stmt, 4);
    if (desc != NULL) {
        cJSON_AddStringToObject(item, "description", desc);
    } else {
        cJSON_AddNullToObject(item, "description");
    }
    const char *cat = (const char *)sqlite3_column_text(stmt, 5);
    cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
    cJSON_AddNumberToObject(item, "topic_count", sqlite3_column_int(stmt, 6));
    sqlite3_finalize(stmt);

    char *json_out = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_lesson_update(sqlite3 *db,
                                 int lesson_id,
                                 int order_index,
                                 const char *name,
                                 const char *description)
{
    if (db == NULL || lesson_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (order_index <= 0 && name == NULL && description == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (name != NULL && is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql = "SELECT chapter_id, order_index, name, description FROM lessons WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    int chapter_id = sqlite3_column_int(stmt, 0);
    int old_order = sqlite3_column_int(stmt, 1);
    char old_name[256];
    char old_desc[1024];
    int has_old_desc = 0;

    const char *cur_name = (const char *)sqlite3_column_text(stmt, 2);
    const char *cur_desc = (const char *)sqlite3_column_text(stmt, 3);
    snprintf(old_name, sizeof(old_name), "%s", cur_name ? cur_name : "");
    if (cur_desc != NULL) {
        has_old_desc = 1;
        snprintf(old_desc, sizeof(old_desc), "%s", cur_desc);
    }
    sqlite3_finalize(stmt);

    int final_order = (order_index > 0) ? order_index : old_order;
    const char *final_name = (name != NULL) ? name : old_name;
    const char *final_desc = (description != NULL) ? description : (has_old_desc ? old_desc : NULL);

    /* Kiem tra trung order_index trong cung chapter */
    if (order_index > 0 && final_order != old_order) {
        const char *dup_sql = "SELECT 1 FROM lessons WHERE chapter_id = ? AND order_index = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_int(stmt, 1, chapter_id);
        sqlite3_bind_int(stmt, 2, final_order);
        sqlite3_bind_int(stmt, 3, lesson_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    /* Kiem tra trung ten trong cung chapter */
    if (name != NULL && strcmp(final_name, old_name) != 0) {
        const char *dup_sql = "SELECT 1 FROM lessons WHERE chapter_id = ? AND name = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_int(stmt, 1, chapter_id);
        sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, lesson_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    const char *upd_sql = "UPDATE lessons SET order_index = ?, name = ?, description = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, upd_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, final_order);
    sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
    if (final_desc != NULL) {
        sqlite3_bind_text(stmt, 3, final_desc, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }
    sqlite3_bind_int(stmt, 4, lesson_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

int classification_lesson_delete(sqlite3 *db,
                                 int lesson_id)
{
    if (db == NULL || lesson_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int ex = lesson_exists(db, lesson_id);
    if (ex == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (ex < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra rang buoc RESTRICT: topic con dang ton tai */
    const char *child_sql = "SELECT 1 FROM topics WHERE lesson_id = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, child_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);
    int in_use = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (in_use) {
        return CLASSIFICATION_ERR_IN_USE;
    }

    const char *del_sql = "DELETE FROM lessons WHERE id = ?;";
    if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * TOPIC IMPLEMENTATION
 * ============================================================================
 */

int classification_topic_create(sqlite3 *db,
                                int lesson_id,
                                const char *name,
                                const char *description,
                                int *out_topic_id)
{
    if (out_topic_id != NULL) {
        *out_topic_id = 0;
    }
    if (db == NULL || lesson_id <= 0 || name == NULL || out_topic_id == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    /* Kiem tra bai hoc cha ton tai */
    int parent = lesson_exists(db, lesson_id);
    if (parent == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (parent < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra trung ten topic trong cung lesson */
    const char *chk_sql = "SELECT 1 FROM topics WHERE lesson_id = ? AND name = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, chk_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    int dup = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (dup) {
        return CLASSIFICATION_ERR_DUPLICATE;
    }

    const char *sql = "INSERT INTO topics (lesson_id, name, description) VALUES (?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, lesson_id);
    sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
    if (description != NULL) {
        sqlite3_bind_text(stmt, 3, description, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    *out_topic_id = (int)sqlite3_last_insert_rowid(db);
    return CLASSIFICATION_SUCCESS;
}

int classification_topic_list(sqlite3 *db,
                              int lesson_id,
                              int limit,
                              int offset,
                              char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    const char *count_sql_filter = "SELECT COUNT(*) FROM topics WHERE lesson_id = ?;";
    const char *count_sql_all = "SELECT COUNT(*) FROM topics;";
    sqlite3_stmt *stmt = NULL;
    int rc = 0;

    if (lesson_id > 0) {
        rc = sqlite3_prepare_v2(db, count_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) sqlite3_bind_int(stmt, 1, lesson_id);
    } else {
        rc = sqlite3_prepare_v2(db, count_sql_all, -1, &stmt, NULL);
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    const char *sel_sql_filter =
        "SELECT t.id, t.lesson_id, t.name, t.description, t.created_at, "
        "       (SELECT COUNT(*) FROM questions q WHERE q.topic_id = t.id) AS question_count "
        "FROM topics t WHERE t.lesson_id = ? "
        "ORDER BY t.id ASC LIMIT ? OFFSET ?;";
    const char *sel_sql_all =
        "SELECT t.id, t.lesson_id, t.name, t.description, t.created_at, "
        "       (SELECT COUNT(*) FROM questions q WHERE q.topic_id = t.id) AS question_count "
        "FROM topics t "
        "ORDER BY t.lesson_id ASC, t.id ASC LIMIT ? OFFSET ?;";

    if (lesson_id > 0) {
        rc = sqlite3_prepare_v2(db, sel_sql_filter, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, lesson_id);
            sqlite3_bind_int(stmt, 2, limit);
            sqlite3_bind_int(stmt, 3, offset);
        }
    } else {
        rc = sqlite3_prepare_v2(db, sel_sql_all, -1, &stmt, NULL);
        if (rc == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, limit);
            sqlite3_bind_int(stmt, 2, offset);
        }
    }
    if (rc != SQLITE_OK || stmt == NULL) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "topics");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddNumberToObject(item, "lesson_id", sqlite3_column_int(stmt, 1));
        cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 2));
        const char *desc = (const char *)sqlite3_column_text(stmt, 3);
        if (desc != NULL) {
            cJSON_AddStringToObject(item, "description", desc);
        } else {
            cJSON_AddNullToObject(item, "description");
        }
        const char *cat = (const char *)sqlite3_column_text(stmt, 4);
        cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
        cJSON_AddNumberToObject(item, "question_count", sqlite3_column_int(stmt, 5));

        cJSON_AddItemToArray(items, item);
        count++;
    }
    sqlite3_finalize(stmt);

    cJSON_AddNumberToObject(root, "total", total);
    cJSON_AddNumberToObject(root, "count", count);
    cJSON_AddNumberToObject(root, "limit", limit);
    cJSON_AddNumberToObject(root, "offset", offset);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_topic_get(sqlite3 *db,
                             int topic_id,
                             char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || topic_id <= 0 || out_json == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql =
        "SELECT t.id, t.lesson_id, t.name, t.description, t.created_at, "
        "       (SELECT COUNT(*) FROM questions q WHERE q.topic_id = t.id) AS question_count "
        "FROM topics t WHERE t.id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, topic_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    cJSON *item = cJSON_CreateObject();
    if (!item) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_MEMORY;
    }
    cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
    cJSON_AddNumberToObject(item, "lesson_id", sqlite3_column_int(stmt, 1));
    cJSON_AddStringToObject(item, "name", (const char *)sqlite3_column_text(stmt, 2));
    const char *desc = (const char *)sqlite3_column_text(stmt, 3);
    if (desc != NULL) {
        cJSON_AddStringToObject(item, "description", desc);
    } else {
        cJSON_AddNullToObject(item, "description");
    }
    const char *cat = (const char *)sqlite3_column_text(stmt, 4);
    cJSON_AddStringToObject(item, "created_at", cat ? cat : "");
    cJSON_AddNumberToObject(item, "question_count", sqlite3_column_int(stmt, 5));
    sqlite3_finalize(stmt);

    char *json_out = cJSON_PrintUnformatted(item);
    cJSON_Delete(item);
    if (!json_out) {
        return CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return CLASSIFICATION_SUCCESS;
}

int classification_topic_update(sqlite3 *db,
                                int topic_id,
                                const char *name,
                                const char *description)
{
    if (db == NULL || topic_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (name == NULL && description == NULL) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (name != NULL && is_blank_string(name)) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sel_sql = "SELECT lesson_id, name, description FROM topics WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, topic_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return CLASSIFICATION_ERR_NOT_FOUND;
    }

    int lesson_id = sqlite3_column_int(stmt, 0);
    char old_name[256];
    char old_desc[1024];
    int has_old_desc = 0;

    const char *cur_name = (const char *)sqlite3_column_text(stmt, 1);
    const char *cur_desc = (const char *)sqlite3_column_text(stmt, 2);
    snprintf(old_name, sizeof(old_name), "%s", cur_name ? cur_name : "");
    if (cur_desc != NULL) {
        has_old_desc = 1;
        snprintf(old_desc, sizeof(old_desc), "%s", cur_desc);
    }
    sqlite3_finalize(stmt);

    const char *final_name = (name != NULL) ? name : old_name;
    const char *final_desc = (description != NULL) ? description : (has_old_desc ? old_desc : NULL);

    /* Kiem tra trung ten trong cung lesson */
    if (name != NULL && strcmp(final_name, old_name) != 0) {
        const char *dup_sql = "SELECT 1 FROM topics WHERE lesson_id = ? AND name = ? AND id != ?;";
        if (sqlite3_prepare_v2(db, dup_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return CLASSIFICATION_ERR_DATABASE;
        }
        sqlite3_bind_int(stmt, 1, lesson_id);
        sqlite3_bind_text(stmt, 2, final_name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, topic_id);
        int dup = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (dup) {
            return CLASSIFICATION_ERR_DUPLICATE;
        }
    }

    const char *upd_sql = "UPDATE topics SET name = ?, description = ? WHERE id = ?;";
    if (sqlite3_prepare_v2(db, upd_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_text(stmt, 1, final_name, -1, SQLITE_STATIC);
    if (final_desc != NULL) {
        sqlite3_bind_text(stmt, 2, final_desc, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt, 2);
    }
    sqlite3_bind_int(stmt, 3, topic_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

int classification_topic_delete(sqlite3 *db,
                                int topic_id)
{
    if (db == NULL || topic_id <= 0) {
        return CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int ex = topic_exists(db, topic_id);
    if (ex == 0) {
        return CLASSIFICATION_ERR_NOT_FOUND;
    } else if (ex < 0) {
        return CLASSIFICATION_ERR_DATABASE;
    }

    /* Kiem tra rang buoc RESTRICT: co cau hoi dang tham chieu toi topic nay */
    const char *q_sql = "SELECT 1 FROM questions WHERE topic_id = ? LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, q_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, topic_id);
    int in_use = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    if (in_use) {
        return CLASSIFICATION_ERR_IN_USE;
    }

    const char *del_sql = "DELETE FROM topics WHERE id = ?;";
    if (sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, topic_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return map_sqlite_error(db, CLASSIFICATION_ERR_DATABASE);
    }

    return CLASSIFICATION_SUCCESS;
}

