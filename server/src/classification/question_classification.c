#include "question_classification.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

/*
 * ============================================================================
 * Helper Functions
 * ============================================================================
 */

static int question_exists(sqlite3 *db, int id)
{
    const char *sql = "SELECT 1 FROM questions WHERE id = ?;";
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

static int is_valid_difficulty(const char *diff)
{
    if (diff == NULL || diff[0] == '\0') {
        return 1;
    }
    return (strcmp(diff, "EASY") == 0 ||
            strcmp(diff, "MEDIUM") == 0 ||
            strcmp(diff, "HARD") == 0);
}

/*
 * ============================================================================
 * 1. question_classification_assign
 * ============================================================================
 */
int question_classification_assign(sqlite3 *db,
                                   int question_id,
                                   int topic_id)
{
    if (db == NULL || question_id <= 0 || topic_id <= 0) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int q_ex = question_exists(db, question_id);
    if (q_ex == 0) {
        return QUESTION_CLASSIFICATION_ERR_NOT_FOUND;
    } else if (q_ex < 0) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }

    int t_ex = topic_exists(db, topic_id);
    if (t_ex == 0) {
        return QUESTION_CLASSIFICATION_ERR_NOT_FOUND;
    } else if (t_ex < 0) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }

    const char *sql = "UPDATE questions SET topic_id = ? WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, topic_id);
    sqlite3_bind_int(stmt, 2, question_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }

    return QUESTION_CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * 2. question_classification_unassign
 * ============================================================================
 */
int question_classification_unassign(sqlite3 *db,
                                     int question_id)
{
    if (db == NULL || question_id <= 0) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }

    int q_ex = question_exists(db, question_id);
    if (q_ex == 0) {
        return QUESTION_CLASSIFICATION_ERR_NOT_FOUND;
    } else if (q_ex < 0) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }

    const char *sql = "UPDATE questions SET topic_id = NULL WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, question_id);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }

    return QUESTION_CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * 3. question_classification_get
 * ============================================================================
 */
int question_classification_get(sqlite3 *db,
                                int question_id,
                                char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || question_id <= 0 || out_json == NULL) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }

    const char *sql =
        "SELECT q.id, q.content, q.topic, q.difficulty, q.created_at, q.topic_id, "
        "       t.name AS topic_name, "
        "       l.id AS lesson_id, l.name AS lesson_name, l.order_index AS lesson_order, "
        "       c.id AS chapter_id, c.name AS chapter_name, c.order_index AS chapter_order, "
        "       s.id AS subject_id, s.code AS subject_code, s.name AS subject_name "
        "FROM questions q "
        "LEFT JOIN topics t ON q.topic_id = t.id "
        "LEFT JOIN lessons l ON t.lesson_id = l.id "
        "LEFT JOIN chapters c ON l.chapter_id = c.id "
        "LEFT JOIN subjects s ON c.subject_id = s.id "
        "WHERE q.id = ?;";

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, question_id);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_NOT_FOUND;
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    cJSON_AddNumberToObject(root, "question_id", sqlite3_column_int(stmt, 0));
    cJSON_AddStringToObject(root, "content", (const char *)sqlite3_column_text(stmt, 1));
    cJSON_AddStringToObject(root, "topic", (const char *)sqlite3_column_text(stmt, 2));
    cJSON_AddStringToObject(root, "difficulty", (const char *)sqlite3_column_text(stmt, 3));
    const char *cat = (const char *)sqlite3_column_text(stmt, 4);
    cJSON_AddStringToObject(root, "created_at", cat ? cat : "");

    int is_unassigned = (sqlite3_column_type(stmt, 5) == SQLITE_NULL);
    if (is_unassigned) {
        cJSON_AddNullToObject(root, "topic_id");
        cJSON_AddNullToObject(root, "topic_name");
        cJSON_AddNullToObject(root, "lesson_id");
        cJSON_AddNullToObject(root, "lesson_name");
        cJSON_AddNullToObject(root, "chapter_id");
        cJSON_AddNullToObject(root, "chapter_name");
        cJSON_AddNullToObject(root, "subject_id");
        cJSON_AddNullToObject(root, "subject_code");
        cJSON_AddNullToObject(root, "subject_name");
        cJSON_AddNullToObject(root, "classification");
    } else {
        int topic_id = sqlite3_column_int(stmt, 5);
        const char *topic_name = (const char *)sqlite3_column_text(stmt, 6);
        int lesson_id = sqlite3_column_int(stmt, 7);
        const char *lesson_name = (const char *)sqlite3_column_text(stmt, 8);
        int lesson_order = sqlite3_column_int(stmt, 9);
        int chapter_id = sqlite3_column_int(stmt, 10);
        const char *chapter_name = (const char *)sqlite3_column_text(stmt, 11);
        int chapter_order = sqlite3_column_int(stmt, 12);
        int subject_id = sqlite3_column_int(stmt, 13);
        const char *subject_code = (const char *)sqlite3_column_text(stmt, 14);
        const char *subject_name = (const char *)sqlite3_column_text(stmt, 15);

        cJSON_AddNumberToObject(root, "topic_id", topic_id);
        cJSON_AddStringToObject(root, "topic_name", topic_name ? topic_name : "");
        cJSON_AddNumberToObject(root, "lesson_id", lesson_id);
        cJSON_AddStringToObject(root, "lesson_name", lesson_name ? lesson_name : "");
        cJSON_AddNumberToObject(root, "chapter_id", chapter_id);
        cJSON_AddStringToObject(root, "chapter_name", chapter_name ? chapter_name : "");
        cJSON_AddNumberToObject(root, "subject_id", subject_id);
        cJSON_AddStringToObject(root, "subject_code", subject_code ? subject_code : "");
        cJSON_AddStringToObject(root, "subject_name", subject_name ? subject_name : "");

        cJSON *cls = cJSON_CreateObject();
        if (cls) {
            cJSON *sub_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(sub_obj, "id", subject_id);
            cJSON_AddStringToObject(sub_obj, "code", subject_code ? subject_code : "");
            cJSON_AddStringToObject(sub_obj, "name", subject_name ? subject_name : "");
            cJSON_AddItemToObject(cls, "subject", sub_obj);

            cJSON *chap_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(chap_obj, "id", chapter_id);
            cJSON_AddNumberToObject(chap_obj, "order_index", chapter_order);
            cJSON_AddStringToObject(chap_obj, "name", chapter_name ? chapter_name : "");
            cJSON_AddItemToObject(cls, "chapter", chap_obj);

            cJSON *les_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(les_obj, "id", lesson_id);
            cJSON_AddNumberToObject(les_obj, "order_index", lesson_order);
            cJSON_AddStringToObject(les_obj, "name", lesson_name ? lesson_name : "");
            cJSON_AddItemToObject(cls, "lesson", les_obj);

            cJSON *top_obj = cJSON_CreateObject();
            cJSON_AddNumberToObject(top_obj, "id", topic_id);
            cJSON_AddStringToObject(top_obj, "name", topic_name ? topic_name : "");
            cJSON_AddItemToObject(cls, "topic", top_obj);

            cJSON_AddItemToObject(root, "classification", cls);
        }
    }
    sqlite3_finalize(stmt);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * 4. question_classification_list
 * ============================================================================
 */
int question_classification_list(sqlite3 *db,
                                 int subject_id,
                                 int chapter_id,
                                 int lesson_id,
                                 int topic_id,
                                 const char *difficulty,
                                 int limit,
                                 int offset,
                                 char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }
    if (difficulty != NULL && difficulty[0] != '\0' && !is_valid_difficulty(difficulty)) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }

    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    char where_sql[512] = "WHERE 1=1 ";
    int param_sub = (subject_id > 0);
    int param_chap = (chapter_id > 0);
    int param_les = (lesson_id > 0);
    int param_top = (topic_id > 0);
    int param_diff = (difficulty != NULL && difficulty[0] != '\0');

    if (param_sub) strcat(where_sql, "AND s.id = ? ");
    if (param_chap) strcat(where_sql, "AND c.id = ? ");
    if (param_les) strcat(where_sql, "AND l.id = ? ");
    if (param_top) strcat(where_sql, "AND t.id = ? ");
    if (param_diff) strcat(where_sql, "AND q.difficulty = ? ");

    char count_sql[1024];
    snprintf(count_sql, sizeof(count_sql),
             "SELECT COUNT(*) "
             "FROM questions q "
             "JOIN topics t ON q.topic_id = t.id "
             "JOIN lessons l ON t.lesson_id = l.id "
             "JOIN chapters c ON l.chapter_id = c.id "
             "JOIN subjects s ON c.subject_id = s.id "
             "%s;", where_sql);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    int bidx = 1;
    if (param_sub) sqlite3_bind_int(stmt, bidx++, subject_id);
    if (param_chap) sqlite3_bind_int(stmt, bidx++, chapter_id);
    if (param_les) sqlite3_bind_int(stmt, bidx++, lesson_id);
    if (param_top) sqlite3_bind_int(stmt, bidx++, topic_id);
    if (param_diff) sqlite3_bind_text(stmt, bidx++, difficulty, -1, SQLITE_STATIC);

    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    char sel_sql[2048];
    snprintf(sel_sql, sizeof(sel_sql),
             "SELECT q.id, q.content, q.topic, q.difficulty, q.created_at, q.topic_id, "
             "       t.name AS topic_name, "
             "       l.id AS lesson_id, l.name AS lesson_name, "
             "       c.id AS chapter_id, c.name AS chapter_name, "
             "       s.id AS subject_id, s.code AS subject_code, s.name AS subject_name, "
             "       (SELECT COUNT(*) FROM answers a WHERE a.question_id = q.id) AS answer_count "
             "FROM questions q "
             "JOIN topics t ON q.topic_id = t.id "
             "JOIN lessons l ON t.lesson_id = l.id "
             "JOIN chapters c ON l.chapter_id = c.id "
             "JOIN subjects s ON c.subject_id = s.id "
             "%s "
             "ORDER BY q.id ASC LIMIT ? OFFSET ?;", where_sql);

    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    bidx = 1;
    if (param_sub) sqlite3_bind_int(stmt, bidx++, subject_id);
    if (param_chap) sqlite3_bind_int(stmt, bidx++, chapter_id);
    if (param_les) sqlite3_bind_int(stmt, bidx++, lesson_id);
    if (param_top) sqlite3_bind_int(stmt, bidx++, topic_id);
    if (param_diff) sqlite3_bind_text(stmt, bidx++, difficulty, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, bidx++, limit);
    sqlite3_bind_int(stmt, bidx++, offset);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "questions");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return QUESTION_CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddStringToObject(item, "content", (const char *)sqlite3_column_text(stmt, 1));
        cJSON_AddStringToObject(item, "topic", (const char *)sqlite3_column_text(stmt, 2));
        cJSON_AddStringToObject(item, "difficulty", (const char *)sqlite3_column_text(stmt, 3));
        const char *item_cat = (const char *)sqlite3_column_text(stmt, 4);
        cJSON_AddStringToObject(item, "created_at", item_cat ? item_cat : "");
        cJSON_AddNumberToObject(item, "topic_id", sqlite3_column_int(stmt, 5));
        const char *tname = (const char *)sqlite3_column_text(stmt, 6);
        cJSON_AddStringToObject(item, "topic_name", tname ? tname : "");
        cJSON_AddNumberToObject(item, "lesson_id", sqlite3_column_int(stmt, 7));
        const char *lname = (const char *)sqlite3_column_text(stmt, 8);
        cJSON_AddStringToObject(item, "lesson_name", lname ? lname : "");
        cJSON_AddNumberToObject(item, "chapter_id", sqlite3_column_int(stmt, 9));
        const char *cname = (const char *)sqlite3_column_text(stmt, 10);
        cJSON_AddStringToObject(item, "chapter_name", cname ? cname : "");
        cJSON_AddNumberToObject(item, "subject_id", sqlite3_column_int(stmt, 11));
        const char *scode = (const char *)sqlite3_column_text(stmt, 12);
        cJSON_AddStringToObject(item, "subject_code", scode ? scode : "");
        const char *sname = (const char *)sqlite3_column_text(stmt, 13);
        cJSON_AddStringToObject(item, "subject_name", sname ? sname : "");
        cJSON_AddNumberToObject(item, "answer_count", sqlite3_column_int(stmt, 14));

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
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_CLASSIFICATION_SUCCESS;
}

/*
 * ============================================================================
 * 5. question_classification_list_unassigned
 * ============================================================================
 */
int question_classification_list_unassigned(sqlite3 *db,
                                            int limit,
                                            int offset,
                                            char **out_json)
{
    if (out_json != NULL) {
        *out_json = NULL;
    }
    if (db == NULL || out_json == NULL) {
        return QUESTION_CLASSIFICATION_ERR_INVALID_PARAM;
    }

    if (limit <= 0 || limit > 100) {
        limit = 20;
    }
    if (offset < 0) {
        offset = 0;
    }

    const char *count_sql = "SELECT COUNT(*) FROM questions WHERE topic_id IS NULL;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    int total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        total = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    const char *sel_sql =
        "SELECT q.id, q.content, q.topic, q.difficulty, q.created_at, "
        "       (SELECT COUNT(*) FROM answers a WHERE a.question_id = q.id) AS answer_count "
        "FROM questions q "
        "WHERE q.topic_id IS NULL "
        "ORDER BY q.id ASC LIMIT ? OFFSET ?;";

    if (sqlite3_prepare_v2(db, sel_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_CLASSIFICATION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, limit);
    sqlite3_bind_int(stmt, 2, offset);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }
    cJSON *items = cJSON_AddArrayToObject(root, "questions");
    if (!items) {
        cJSON_Delete(root);
        sqlite3_finalize(stmt);
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    int count = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            sqlite3_finalize(stmt);
            return QUESTION_CLASSIFICATION_ERR_MEMORY;
        }
        cJSON_AddNumberToObject(item, "id", sqlite3_column_int(stmt, 0));
        cJSON_AddStringToObject(item, "content", (const char *)sqlite3_column_text(stmt, 1));
        cJSON_AddStringToObject(item, "topic", (const char *)sqlite3_column_text(stmt, 2));
        cJSON_AddStringToObject(item, "difficulty", (const char *)sqlite3_column_text(stmt, 3));
        const char *item_cat = (const char *)sqlite3_column_text(stmt, 4);
        cJSON_AddStringToObject(item, "created_at", item_cat ? item_cat : "");
        cJSON_AddNullToObject(item, "topic_id");
        cJSON_AddNumberToObject(item, "answer_count", sqlite3_column_int(stmt, 5));

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
        return QUESTION_CLASSIFICATION_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_CLASSIFICATION_SUCCESS;
}

