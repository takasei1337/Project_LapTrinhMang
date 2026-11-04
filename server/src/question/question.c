#include "question.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

typedef struct {
    const char *content;
    int is_correct;
} ParsedAnswer;

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

static int db_begin_transaction(sqlite3 *db)
{
    return sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", NULL, NULL, NULL);
}

static void db_rollback(sqlite3 *db)
{
    sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
}

static int db_commit(sqlite3 *db)
{
    return sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
}

static int parse_and_validate_answers(const char *answers_json,
                                      cJSON **out_root,
                                      ParsedAnswer **out_answers,
                                      int *out_num_answers)
{
    if (out_root != NULL) *out_root = NULL;
    if (out_answers != NULL) *out_answers = NULL;
    if (out_num_answers != NULL) *out_num_answers = 0;

    cJSON *root = cJSON_ParseWithOpts(answers_json, NULL, 1);
    if (root == NULL || !cJSON_IsArray(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        return QUESTION_ERR_INVALID_DATA;
    }

    int num_answers = cJSON_GetArraySize(root);
    if (num_answers < 2) {
        cJSON_Delete(root);
        return QUESTION_ERR_INVALID_DATA;
    }

    ParsedAnswer *answers = (ParsedAnswer *)malloc(sizeof(ParsedAnswer) * (size_t)num_answers);
    if (answers == NULL) {
        cJSON_Delete(root);
        return QUESTION_ERR_MEMORY;
    }

    int correct_count = 0;

    for (int i = 0; i < num_answers; i++) {
        cJSON *item = cJSON_GetArrayItem(root, i);
        if (!cJSON_IsObject(item)) {
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_INVALID_DATA;
        }

        /* Kiem tra khoa content va is_correct khong duoc trung lap */
        int count_content = 0;
        int count_is_correct = 0;
        for (cJSON *child = item->child; child != NULL; child = child->next) {
            if (child->string != NULL) {
                if (strcmp(child->string, "content") == 0) {
                    count_content++;
                } else if (strcmp(child->string, "is_correct") == 0) {
                    count_is_correct++;
                }
            }
        }
        if (count_content != 1 || count_is_correct != 1) {
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_INVALID_DATA;
        }

        cJSON *c_item = cJSON_GetObjectItemCaseSensitive(item, "content");
        cJSON *ic_item = cJSON_GetObjectItemCaseSensitive(item, "is_correct");

        if (c_item == NULL || !cJSON_IsString(c_item) || c_item->valuestring == NULL ||
            is_blank_string(c_item->valuestring)) {
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_INVALID_DATA;
        }

        if (ic_item == NULL || !cJSON_IsNumber(ic_item) ||
            ic_item->valuedouble != (double)ic_item->valueint ||
            (ic_item->valueint != 0 && ic_item->valueint != 1)) {
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_INVALID_DATA;
        }

        if (ic_item->valueint == 1) {
            correct_count++;
        }

        answers[i].content = c_item->valuestring;
        answers[i].is_correct = ic_item->valueint;
    }

    /* Bat buoc phai co dung duy nhat 1 dap an dung */
    if (correct_count != 1) {
        free(answers);
        cJSON_Delete(root);
        return QUESTION_ERR_INVALID_DATA;
    }

    *out_root = root;
    *out_answers = answers;
    *out_num_answers = num_answers;
    return QUESTION_SUCCESS;
}

int question_create(sqlite3 *db,
                    const char *content,
                    const char *topic,
                    const char *difficulty,
                    const char *answers_json,
                    int *out_question_id)
{
    /* 1. Dat gia tri mac dinh cho out_question_id */
    if (out_question_id == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }
    *out_question_id = 0;

    /* 2. Kiem tra hop le cac tham so co ban */
    if (db == NULL || content == NULL || topic == NULL ||
        difficulty == NULL || answers_json == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (is_blank_string(content) || is_blank_string(topic)) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (strcmp(difficulty, "EASY") != 0 &&
        strcmp(difficulty, "MEDIUM") != 0 &&
        strcmp(difficulty, "HARD") != 0) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    /* 3. Parse va validate answers_json truoc khi bat dau transaction */
    cJSON *root = NULL;
    ParsedAnswer *answers = NULL;
    int num_answers = 0;
    int parse_rc = parse_and_validate_answers(answers_json, &root, &answers, &num_answers);
    if (parse_rc != QUESTION_SUCCESS) {
        return parse_rc;
    }

    /* 4. Bat dau transaction */
    if (db_begin_transaction(db) != SQLITE_OK) {
        free(answers);
        cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    /* 5. INSERT cau hoi vao bang questions */
    const char *sql_ins_q = "INSERT INTO questions(content, topic, difficulty) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt_q = NULL;
    int rc = sqlite3_prepare_v2(db, sql_ins_q, -1, &stmt_q, NULL);
    if (rc != SQLITE_OK || stmt_q == NULL) {
        free(answers);
        cJSON_Delete(root);
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_text(stmt_q, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_q, 2, topic, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_q, 3, difficulty, -1, SQLITE_STATIC);

    int step_q = sqlite3_step(stmt_q);
    sqlite3_finalize(stmt_q);

    if (step_q != SQLITE_DONE) {
        free(answers);
        cJSON_Delete(root);
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    int new_question_id = (int)sqlite3_last_insert_rowid(db);
    if (new_question_id <= 0) {
        free(answers);
        cJSON_Delete(root);
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    /* 6. INSERT toan bo dap an vao bang answers */
    const char *sql_ins_a = "INSERT INTO answers(question_id, content, is_correct) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt_a = NULL;
    rc = sqlite3_prepare_v2(db, sql_ins_a, -1, &stmt_a, NULL);
    if (rc != SQLITE_OK || stmt_a == NULL) {
        free(answers);
        cJSON_Delete(root);
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    for (int i = 0; i < num_answers; i++) {
        sqlite3_bind_int(stmt_a, 1, new_question_id);
        sqlite3_bind_text(stmt_a, 2, answers[i].content, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt_a, 3, answers[i].is_correct);

        int step_a = sqlite3_step(stmt_a);
        sqlite3_reset(stmt_a);

        if (step_a != SQLITE_DONE) {
            sqlite3_finalize(stmt_a);
            free(answers);
            cJSON_Delete(root);
            db_rollback(db);
            return QUESTION_ERR_DATABASE;
        }
    }
    sqlite3_finalize(stmt_a);

    /* 7. COMMIT transaction */
    if (db_commit(db) != SQLITE_OK) {
        free(answers);
        cJSON_Delete(root);
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    /* Giai phong bo nho va tra ve ket qua */
    free(answers);
    cJSON_Delete(root);

    *out_question_id = new_question_id;
    return QUESTION_SUCCESS;
}

int question_get_detail(sqlite3 *db,
                        int question_id,
                        char **out_json)
{
    /* 1. Dat gia tri mac dinh cho out_json */
    if (out_json == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }
    *out_json = NULL;

    /* 2. Kiem tra tham so co ban */
    if (db == NULL || question_id <= 0) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    /* 3. Truy van thong tin cau hoi */
    const char *sql_q =
        "SELECT id, content, topic, difficulty, created_at "
        "FROM questions "
        "WHERE id = ?;";
    sqlite3_stmt *stmt_q = NULL;
    int rc = sqlite3_prepare_v2(db, sql_q, -1, &stmt_q, NULL);
    if (rc != SQLITE_OK || stmt_q == NULL) {
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_q, 1, question_id);
    int step_q = sqlite3_step(stmt_q);
    if (step_q == SQLITE_DONE) {
        sqlite3_finalize(stmt_q);
        return QUESTION_ERR_NOT_FOUND;
    }
    if (step_q != SQLITE_ROW) {
        sqlite3_finalize(stmt_q);
        return QUESTION_ERR_DATABASE;
    }

    int q_id = sqlite3_column_int(stmt_q, 0);
    const char *q_content = (const char *)sqlite3_column_text(stmt_q, 1);
    const char *q_topic = (const char *)sqlite3_column_text(stmt_q, 2);
    const char *q_diff = (const char *)sqlite3_column_text(stmt_q, 3);
    const char *q_created = (const char *)sqlite3_column_text(stmt_q, 4);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        sqlite3_finalize(stmt_q);
        return QUESTION_ERR_MEMORY;
    }

    cJSON_AddNumberToObject(root, "question_id", q_id);
    cJSON_AddStringToObject(root, "content", q_content ? q_content : "");
    cJSON_AddStringToObject(root, "topic", q_topic ? q_topic : "");
    cJSON_AddStringToObject(root, "difficulty", q_diff ? q_diff : "");
    cJSON_AddStringToObject(root, "created_at", q_created ? q_created : "");

    sqlite3_finalize(stmt_q);

    /* 4. Truy van danh sach dap an */
    cJSON *answers_arr = cJSON_AddArrayToObject(root, "answers");
    if (answers_arr == NULL) {
        cJSON_Delete(root);
        return QUESTION_ERR_MEMORY;
    }

    const char *sql_a =
        "SELECT id, content, is_correct "
        "FROM answers "
        "WHERE question_id = ? "
        "ORDER BY id ASC;";
    sqlite3_stmt *stmt_a = NULL;
    rc = sqlite3_prepare_v2(db, sql_a, -1, &stmt_a, NULL);
    if (rc != SQLITE_OK || stmt_a == NULL) {
        cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_a, 1, question_id);
    int step_a_rc = SQLITE_DONE;
    while ((step_a_rc = sqlite3_step(stmt_a)) == SQLITE_ROW) {
        int a_id = sqlite3_column_int(stmt_a, 0);
        const char *a_content = (const char *)sqlite3_column_text(stmt_a, 1);
        int a_is_correct = sqlite3_column_int(stmt_a, 2);

        cJSON *a_obj = cJSON_CreateObject();
        if (a_obj == NULL) {
            sqlite3_finalize(stmt_a);
            cJSON_Delete(root);
            return QUESTION_ERR_MEMORY;
        }
        cJSON_AddItemToArray(answers_arr, a_obj);

        cJSON_AddNumberToObject(a_obj, "answer_id", a_id);
        cJSON_AddStringToObject(a_obj, "content", a_content ? a_content : "");
        cJSON_AddNumberToObject(a_obj, "is_correct", a_is_correct);
    }
    sqlite3_finalize(stmt_a);

    if (step_a_rc != SQLITE_DONE && step_a_rc != SQLITE_ROW) {
        cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    /* 5. Chuyen sang chuoi JSON */
    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_out == NULL) {
        return QUESTION_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_SUCCESS;
}

int question_list(sqlite3 *db,
                  const char *topic,
                  const char *difficulty,
                  int limit,
                  int offset,
                  char **out_json)
{
    /* 1. Dat gia tri mac dinh cho out_json */
    if (out_json == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }
    *out_json = NULL;

    /* 2. Kiem tra tinh hop le cua tham so */
    if (db == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (topic != NULL && is_blank_string(topic)) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (difficulty != NULL) {
        if (strcmp(difficulty, "EASY") != 0 &&
            strcmp(difficulty, "MEDIUM") != 0 &&
            strcmp(difficulty, "HARD") != 0) {
            return QUESTION_ERR_INVALID_PARAM;
        }
    }

    if (limit < 1 || limit > 100) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (offset < 0) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    /* 3. Chuan bi cau truy van SQLite */
    const char *sql =
        "SELECT id, content, topic, difficulty, created_at "
        "FROM questions "
        "WHERE (?1 IS NULL OR topic = ?1) "
        "  AND (?2 IS NULL OR difficulty = ?2) "
        "ORDER BY id ASC "
        "LIMIT ?3 OFFSET ?4;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return QUESTION_ERR_DATABASE;
    }

    /* Bind param 1: topic */
    if (topic == NULL) {
        sqlite3_bind_null(stmt, 1);
    } else {
        sqlite3_bind_text(stmt, 1, topic, -1, SQLITE_STATIC);
    }

    /* Bind param 2: difficulty */
    if (difficulty == NULL) {
        sqlite3_bind_null(stmt, 2);
    } else {
        sqlite3_bind_text(stmt, 2, difficulty, -1, SQLITE_STATIC);
    }

    /* Bind param 3 & 4: limit va offset */
    sqlite3_bind_int(stmt, 3, limit);
    sqlite3_bind_int(stmt, 4, offset);

    /* 4. Khoi tao JSON root */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        sqlite3_finalize(stmt);
        return QUESTION_ERR_MEMORY;
    }

    cJSON *q_array = cJSON_AddArrayToObject(root, "questions");
    if (q_array == NULL) {
        sqlite3_finalize(stmt);
        cJSON_Delete(root);
        return QUESTION_ERR_MEMORY;
    }

    int count = 0;
    int step_rc = SQLITE_DONE;

    while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int q_id = sqlite3_column_int(stmt, 0);
        const char *q_content = (const char *)sqlite3_column_text(stmt, 1);
        const char *q_topic = (const char *)sqlite3_column_text(stmt, 2);
        const char *q_diff = (const char *)sqlite3_column_text(stmt, 3);
        const char *q_created = (const char *)sqlite3_column_text(stmt, 4);

        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            sqlite3_finalize(stmt);
            cJSON_Delete(root);
            return QUESTION_ERR_MEMORY;
        }
        cJSON_AddItemToArray(q_array, item);

        if (cJSON_AddNumberToObject(item, "question_id", q_id) == NULL ||
            cJSON_AddStringToObject(item, "content", q_content ? q_content : "") == NULL ||
            cJSON_AddStringToObject(item, "topic", q_topic ? q_topic : "") == NULL ||
            cJSON_AddStringToObject(item, "difficulty", q_diff ? q_diff : "") == NULL ||
            cJSON_AddStringToObject(item, "created_at", q_created ? q_created : "") == NULL) {
            sqlite3_finalize(stmt);
            cJSON_Delete(root);
            return QUESTION_ERR_MEMORY;
        }

        count++;
    }

    sqlite3_finalize(stmt);

    if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
        cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    if (cJSON_AddNumberToObject(root, "count", count) == NULL ||
        cJSON_AddNumberToObject(root, "limit", limit) == NULL ||
        cJSON_AddNumberToObject(root, "offset", offset) == NULL) {
        cJSON_Delete(root);
        return QUESTION_ERR_MEMORY;
    }

    /* 5. Chuyen doi sang chuoi JSON */
    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_out == NULL) {
        return QUESTION_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_SUCCESS;
}

static int db_check_question_exists(sqlite3 *db, int question_id)
{
    const char *sql = "SELECT 1 FROM questions WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return QUESTION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, question_id);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step == SQLITE_DONE) {
        return QUESTION_ERR_NOT_FOUND;
    }
    if (step == SQLITE_ROW) {
        return QUESTION_SUCCESS;
    }
    return QUESTION_ERR_DATABASE;
}

static int db_check_question_in_use(sqlite3 *db, int question_id)
{
    const char *sql =
        "SELECT 1 FROM practice_questions WHERE question_id = ?1 "
        "UNION ALL "
        "SELECT 1 FROM room_questions WHERE question_id = ?1 "
        "UNION ALL "
        "SELECT 1 FROM attempt_answers WHERE question_id = ?1 "
        "LIMIT 1;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return QUESTION_ERR_DATABASE;
    }
    sqlite3_bind_int(stmt, 1, question_id);
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step == SQLITE_ROW) {
        return QUESTION_ERR_IN_USE;
    }
    if (step == SQLITE_DONE) {
        return QUESTION_SUCCESS;
    }
    return QUESTION_ERR_DATABASE;
}

int question_update(sqlite3 *db,
                    int question_id,
                    const char *content,
                    const char *topic,
                    const char *difficulty,
                    const char *answers_json)
{
    /* 1. Kiem tra tham so co ban */
    if (db == NULL || question_id <= 0 || content == NULL ||
        topic == NULL || difficulty == NULL) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (is_blank_string(content) || is_blank_string(topic)) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    if (strcmp(difficulty, "EASY") != 0 &&
        strcmp(difficulty, "MEDIUM") != 0 &&
        strcmp(difficulty, "HARD") != 0) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    /* 2. Neu answers_json != NULL, parse va validate truoc khi bat dau transaction */
    cJSON *root = NULL;
    ParsedAnswer *answers = NULL;
    int num_answers = 0;

    if (answers_json != NULL) {
        int parse_rc = parse_and_validate_answers(answers_json, &root, &answers, &num_answers);
        if (parse_rc != QUESTION_SUCCESS) {
            return parse_rc;
        }
    }

    /* 3. Bat dau transaction */
    if (db_begin_transaction(db) != SQLITE_OK) {
        if (answers != NULL) free(answers);
        if (root != NULL) cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    /* 4. Kiem tra cau hoi co ton tai hay khong */
    int exists_rc = db_check_question_exists(db, question_id);
    if (exists_rc != QUESTION_SUCCESS) {
        db_rollback(db);
        if (answers != NULL) free(answers);
        if (root != NULL) cJSON_Delete(root);
        return exists_rc;
    }

    /* 5. Kiem tra xem cau hoi da duoc tham chieu hay chua (IN_USE) */
    int in_use_rc = db_check_question_in_use(db, question_id);
    if (in_use_rc != QUESTION_SUCCESS) {
        db_rollback(db);
        if (answers != NULL) free(answers);
        if (root != NULL) cJSON_Delete(root);
        return in_use_rc;
    }

    /* 6. UPDATE bang questions */
    const char *sql_update_q =
        "UPDATE questions "
        "SET content = ?, topic = ?, difficulty = ? "
        "WHERE id = ?;";
    sqlite3_stmt *stmt_up = NULL;
    int rc = sqlite3_prepare_v2(db, sql_update_q, -1, &stmt_up, NULL);
    if (rc != SQLITE_OK || stmt_up == NULL) {
        db_rollback(db);
        if (answers != NULL) free(answers);
        if (root != NULL) cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_text(stmt_up, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_up, 2, topic, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_up, 3, difficulty, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt_up, 4, question_id);

    int step_up = sqlite3_step(stmt_up);
    sqlite3_finalize(stmt_up);

    if (step_up != SQLITE_DONE) {
        db_rollback(db);
        if (answers != NULL) free(answers);
        if (root != NULL) cJSON_Delete(root);
        return QUESTION_ERR_DATABASE;
    }

    /* 7. Neu answers_json != NULL, thay the toan bo dap an */
    if (answers_json != NULL) {
        /* Xoa dap an cu */
        const char *sql_del_a = "DELETE FROM answers WHERE question_id = ?;";
        sqlite3_stmt *stmt_del = NULL;
        rc = sqlite3_prepare_v2(db, sql_del_a, -1, &stmt_del, NULL);
        if (rc != SQLITE_OK || stmt_del == NULL) {
            db_rollback(db);
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_DATABASE;
        }

        sqlite3_bind_int(stmt_del, 1, question_id);
        int step_del = sqlite3_step(stmt_del);
        sqlite3_finalize(stmt_del);

        if (step_del != SQLITE_DONE) {
            db_rollback(db);
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_DATABASE;
        }

        /* INSERT dap an moi */
        const char *sql_ins_a = "INSERT INTO answers(question_id, content, is_correct) VALUES (?, ?, ?);";
        sqlite3_stmt *stmt_ins = NULL;
        rc = sqlite3_prepare_v2(db, sql_ins_a, -1, &stmt_ins, NULL);
        if (rc != SQLITE_OK || stmt_ins == NULL) {
            db_rollback(db);
            free(answers);
            cJSON_Delete(root);
            return QUESTION_ERR_DATABASE;
        }

        for (int i = 0; i < num_answers; i++) {
            sqlite3_bind_int(stmt_ins, 1, question_id);
            sqlite3_bind_text(stmt_ins, 2, answers[i].content, -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt_ins, 3, answers[i].is_correct);

            int step_a = sqlite3_step(stmt_ins);
            sqlite3_reset(stmt_ins);

            if (step_a != SQLITE_DONE) {
                sqlite3_finalize(stmt_ins);
                db_rollback(db);
                free(answers);
                cJSON_Delete(root);
                return QUESTION_ERR_DATABASE;
            }
        }
        sqlite3_finalize(stmt_ins);

        free(answers);
        cJSON_Delete(root);
    }

    /* 8. Commit transaction */
    if (db_commit(db) != SQLITE_OK) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    return QUESTION_SUCCESS;
}

int question_delete(sqlite3 *db, int question_id)
{
    /* 1. Kiem tra tham so co ban */
    if (db == NULL || question_id <= 0) {
        return QUESTION_ERR_INVALID_PARAM;
    }

    /* 2. Bat dau transaction */
    if (db_begin_transaction(db) != SQLITE_OK) {
        return QUESTION_ERR_DATABASE;
    }

    /* 3. Kiem tra cau hoi ton tai */
    int exists_rc = db_check_question_exists(db, question_id);
    if (exists_rc != QUESTION_SUCCESS) {
        db_rollback(db);
        return exists_rc;
    }

    /* 4. Kiem tra IN_USE */
    int in_use_rc = db_check_question_in_use(db, question_id);
    if (in_use_rc != QUESTION_SUCCESS) {
        db_rollback(db);
        return in_use_rc;
    }

    /* 5. Xoa tat ca dap an thuoc cau hoi */
    const char *sql_del_a = "DELETE FROM answers WHERE question_id = ?;";
    sqlite3_stmt *stmt_del_a = NULL;
    int rc = sqlite3_prepare_v2(db, sql_del_a, -1, &stmt_del_a, NULL);
    if (rc != SQLITE_OK || stmt_del_a == NULL) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_del_a, 1, question_id);
    int step_del_a = sqlite3_step(stmt_del_a);
    sqlite3_finalize(stmt_del_a);

    if (step_del_a != SQLITE_DONE) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    /* 6. Xoa cau hoi */
    const char *sql_del_q = "DELETE FROM questions WHERE id = ?;";
    sqlite3_stmt *stmt_del_q = NULL;
    rc = sqlite3_prepare_v2(db, sql_del_q, -1, &stmt_del_q, NULL);
    if (rc != SQLITE_OK || stmt_del_q == NULL) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_del_q, 1, question_id);
    int step_del_q = sqlite3_step(stmt_del_q);
    sqlite3_finalize(stmt_del_q);

    if (step_del_q != SQLITE_DONE) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    /* 7. Commit transaction */
    if (db_commit(db) != SQLITE_OK) {
        db_rollback(db);
        return QUESTION_ERR_DATABASE;
    }

    return QUESTION_SUCCESS;
}

