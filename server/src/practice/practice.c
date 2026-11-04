#include "practice.h"

#include <cjson/cJSON.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>

#define MAX_PRACTICE_QUESTIONS 50

typedef struct {
    int id;
    char *content;
    char *topic;
    char *difficulty;
} SelectedQuestion;

static char *string_duplicate(const char *src)
{
    if (src == NULL) {
        return NULL;
    }
    size_t len = strlen(src);
    char *dst = (char *)malloc(len + 1);
    if (dst == NULL) {
        return NULL;
    }
    memcpy(dst, src, len + 1);
    return dst;
}

static void free_selected_questions(SelectedQuestion *questions, int count)
{
    if (questions == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(questions[i].content);
        free(questions[i].topic);
        free(questions[i].difficulty);
    }
    free(questions);
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

static int check_user_exists(sqlite3 *db, int user_id)
{
    const char *sql = "SELECT id FROM users WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || stmt == NULL) {
        return -1;
    }

    sqlite3_bind_int(stmt, 1, user_id);
    int step_rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (step_rc == SQLITE_ROW) {
        return 1;
    }
    if (step_rc == SQLITE_DONE) {
        return 0;
    }
    return -1;
}

int practice_start(sqlite3 *db,
                   int user_id,
                   const char *topic,
                   const char *difficulty,
                   int num_questions,
                   char **out_questions_json)
{
    /* 1. Kiem tra tham so dau vao */
    if (out_questions_json == NULL) {
        return PRACTICE_ERR_INVALID_PARAM;
    }
    *out_questions_json = NULL;

    if (db == NULL || user_id <= 0 || num_questions <= 0 ||
        num_questions > MAX_PRACTICE_QUESTIONS) {
        return PRACTICE_ERR_INVALID_PARAM;
    }

    if (topic != NULL && topic[0] == '\0') {
        return PRACTICE_ERR_INVALID_PARAM;
    }

    if (difficulty != NULL) {
        if (strcmp(difficulty, "EASY") != 0 &&
            strcmp(difficulty, "MEDIUM") != 0 &&
            strcmp(difficulty, "HARD") != 0) {
            return PRACTICE_ERR_INVALID_PARAM;
        }
    }

    /* 2. Khoi tao Transaction */
    if (db_begin_transaction(db) != SQLITE_OK) {
        return PRACTICE_ERR_DATABASE;
    }

    /* 3. Xac minh user_id ton tai trong bang users */
    int user_check = check_user_exists(db, user_id);
    if (user_check == 0) {
        db_rollback(db);
        return PRACTICE_ERR_NOT_FOUND;
    }
    if (user_check < 0) {
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    /* 4. Chon ngau nhien num_questions cau hoi hop le */
    SelectedQuestion *questions = (SelectedQuestion *)malloc(sizeof(SelectedQuestion) * (size_t)num_questions);
    if (questions == NULL) {
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }
    memset(questions, 0, sizeof(SelectedQuestion) * (size_t)num_questions);

    const char *sql_select_questions =
        "SELECT q.id, q.content, q.topic, q.difficulty "
        "FROM questions q "
        "JOIN answers a ON q.id = a.question_id "
        "WHERE (?1 IS NULL OR q.topic = ?1) "
        "  AND (?2 IS NULL OR q.difficulty = ?2) "
        "GROUP BY q.id, q.content, q.topic, q.difficulty "
        "HAVING COUNT(a.id) >= 2 AND SUM(a.is_correct) = 1 "
        "ORDER BY RANDOM() "
        "LIMIT ?3;";

    sqlite3_stmt *stmt_q = NULL;
    int rc = sqlite3_prepare_v2(db, sql_select_questions, -1, &stmt_q, NULL);
    if (rc != SQLITE_OK || stmt_q == NULL) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    if (topic != NULL) {
        sqlite3_bind_text(stmt_q, 1, topic, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt_q, 1);
    }

    if (difficulty != NULL) {
        sqlite3_bind_text(stmt_q, 2, difficulty, -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_null(stmt_q, 2);
    }

    sqlite3_bind_int(stmt_q, 3, num_questions);

    int count_fetched = 0;
    int alloc_error = 0;
    int step_q_rc = SQLITE_DONE;

    while ((step_q_rc = sqlite3_step(stmt_q)) == SQLITE_ROW) {
        if (count_fetched < num_questions) {
            int q_id = sqlite3_column_int(stmt_q, 0);
            const char *q_content = (const char *)sqlite3_column_text(stmt_q, 1);
            const char *q_topic = (const char *)sqlite3_column_text(stmt_q, 2);
            const char *q_diff = (const char *)sqlite3_column_text(stmt_q, 3);

            questions[count_fetched].id = q_id;
            questions[count_fetched].content = string_duplicate(q_content);
            questions[count_fetched].topic = string_duplicate(q_topic);
            questions[count_fetched].difficulty = string_duplicate(q_diff);

            if (questions[count_fetched].content == NULL ||
                questions[count_fetched].topic == NULL ||
                questions[count_fetched].difficulty == NULL) {
                alloc_error = 1;
                break;
            }
            count_fetched++;
        }
    }
    sqlite3_finalize(stmt_q);

    if (alloc_error) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    if (step_q_rc != SQLITE_DONE && step_q_rc != SQLITE_ROW) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    if (count_fetched < num_questions) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_NOT_ENOUGH_QUESTIONS;
    }

    /* 5. Tao ban ghi exam_attempts voi mode = 'PRACTICE', room_id = NULL */
    const char *sql_insert_attempt =
        "INSERT INTO exam_attempts (room_id, user_id, mode) VALUES (NULL, ?, 'PRACTICE');";
    sqlite3_stmt *stmt_att = NULL;
    rc = sqlite3_prepare_v2(db, sql_insert_attempt, -1, &stmt_att, NULL);
    if (rc != SQLITE_OK || stmt_att == NULL) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_att, 1, user_id);
    int step_att = sqlite3_step(stmt_att);
    sqlite3_finalize(stmt_att);

    if (step_att != SQLITE_DONE) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    int attempt_id = (int)sqlite3_last_insert_rowid(db);
    if (attempt_id <= 0) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    /* 6. Ghi danh sach cau hoi vao bang practice_questions voi position bat dau tu 1 */
    const char *sql_insert_pq =
        "INSERT INTO practice_questions (attempt_id, question_id, position) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt_pq = NULL;
    rc = sqlite3_prepare_v2(db, sql_insert_pq, -1, &stmt_pq, NULL);
    if (rc != SQLITE_OK || stmt_pq == NULL) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    for (int i = 0; i < num_questions; i++) {
        sqlite3_bind_int(stmt_pq, 1, attempt_id);
        sqlite3_bind_int(stmt_pq, 2, questions[i].id);
        sqlite3_bind_int(stmt_pq, 3, i + 1);

        int step_pq = sqlite3_step(stmt_pq);
        sqlite3_reset(stmt_pq);

        if (step_pq != SQLITE_DONE) {
            sqlite3_finalize(stmt_pq);
            free_selected_questions(questions, num_questions);
            db_rollback(db);
            return PRACTICE_ERR_DATABASE;
        }
    }
    sqlite3_finalize(stmt_pq);

    /* 7. Xay dung chuoi JSON bang cJSON (khong lay cot is_correct) */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    cJSON_AddNumberToObject(root, "attempt_id", attempt_id);
    cJSON_AddStringToObject(root, "mode", "PRACTICE");
    cJSON_AddNumberToObject(root, "total_questions", num_questions);

    cJSON *questions_arr = cJSON_AddArrayToObject(root, "questions");
    if (questions_arr == NULL) {
        cJSON_Delete(root);
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    const char *sql_select_answers =
        "SELECT id, content FROM answers WHERE question_id = ? ORDER BY id ASC;";
    sqlite3_stmt *stmt_ans = NULL;
    rc = sqlite3_prepare_v2(db, sql_select_answers, -1, &stmt_ans, NULL);
    if (rc != SQLITE_OK || stmt_ans == NULL) {
        cJSON_Delete(root);
        free_selected_questions(questions, num_questions);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    for (int i = 0; i < num_questions; i++) {
        cJSON *q_obj = cJSON_CreateObject();
        if (q_obj == NULL) {
            sqlite3_finalize(stmt_ans);
            cJSON_Delete(root);
            free_selected_questions(questions, num_questions);
            db_rollback(db);
            return PRACTICE_ERR_MEMORY;
        }

        /* Gan q_obj vao questions_arr ngay de root quan ly giai phong */
        cJSON_AddItemToArray(questions_arr, q_obj);

        cJSON_AddNumberToObject(q_obj, "question_id", questions[i].id);
        cJSON_AddStringToObject(q_obj, "content", questions[i].content);
        cJSON_AddStringToObject(q_obj, "topic", questions[i].topic);
        cJSON_AddStringToObject(q_obj, "difficulty", questions[i].difficulty);

        cJSON *answers_arr = cJSON_AddArrayToObject(q_obj, "answers");
        if (answers_arr == NULL) {
            sqlite3_finalize(stmt_ans);
            cJSON_Delete(root);
            free_selected_questions(questions, num_questions);
            db_rollback(db);
            return PRACTICE_ERR_MEMORY;
        }

        sqlite3_bind_int(stmt_ans, 1, questions[i].id);
        int step_ans_rc = SQLITE_DONE;
        while ((step_ans_rc = sqlite3_step(stmt_ans)) == SQLITE_ROW) {
            int ans_id = sqlite3_column_int(stmt_ans, 0);
            const char *ans_content = (const char *)sqlite3_column_text(stmt_ans, 1);

            cJSON *ans_obj = cJSON_CreateObject();
            if (ans_obj == NULL) {
                sqlite3_finalize(stmt_ans);
                cJSON_Delete(root);
                free_selected_questions(questions, num_questions);
                db_rollback(db);
                return PRACTICE_ERR_MEMORY;
            }
            cJSON_AddNumberToObject(ans_obj, "answer_id", ans_id);
            cJSON_AddStringToObject(ans_obj, "content", ans_content ? ans_content : "");
            cJSON_AddItemToArray(answers_arr, ans_obj);
        }
        sqlite3_reset(stmt_ans);

        if (step_ans_rc != SQLITE_DONE && step_ans_rc != SQLITE_ROW) {
            sqlite3_finalize(stmt_ans);
            cJSON_Delete(root);
            free_selected_questions(questions, num_questions);
            db_rollback(db);
            return PRACTICE_ERR_DATABASE;
        }
    }
    sqlite3_finalize(stmt_ans);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free_selected_questions(questions, num_questions);

    if (json_out == NULL) {
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    /* 8. Commit Transaction */
    if (db_commit(db) != SQLITE_OK) {
        free(json_out);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    *out_questions_json = json_out;
    return attempt_id;
}

typedef struct {
    int question_id;
    int answer_id;
} SubmittedAnswer;

typedef struct {
    int question_id;
    int position;
    int correct_answer_id;
    int chosen_answer_id;
    int is_correct;
} PracticeDetail;

int practice_submit(sqlite3 *db,
                    int attempt_id,
                    int user_id,
                    const char *answers_json,
                    char **out_result_json)
{
    /* 1. Kiem tra tham so dau vao co ban */
    if (out_result_json == NULL) {
        return PRACTICE_ERR_INVALID_PARAM;
    }
    *out_result_json = NULL;

    if (db == NULL || attempt_id <= 0 || user_id <= 0 || answers_json == NULL) {
        return PRACTICE_ERR_INVALID_PARAM;
    }

    /* 2. Parse va validate cau truc mang JSON answers_json */
    cJSON *answers_arr = cJSON_ParseWithOpts(answers_json, NULL, 1);
    if (answers_arr == NULL || !cJSON_IsArray(answers_arr)) {
        if (answers_arr != NULL) {
            cJSON_Delete(answers_arr);
        }
        return PRACTICE_ERR_INVALID_PARAM;
    }

    int num_answers = cJSON_GetArraySize(answers_arr);
    SubmittedAnswer *sub_answers = NULL;
    if (num_answers > 0) {
        sub_answers = (SubmittedAnswer *)malloc(sizeof(SubmittedAnswer) * (size_t)num_answers);
        if (sub_answers == NULL) {
            cJSON_Delete(answers_arr);
            return PRACTICE_ERR_MEMORY;
        }

        for (int i = 0; i < num_answers; i++) {
            cJSON *item = cJSON_GetArrayItem(answers_arr, i);
            if (!cJSON_IsObject(item)) {
                free(sub_answers);
                cJSON_Delete(answers_arr);
                return PRACTICE_ERR_INVALID_PARAM;
            }

            /* Kiem tra khong co khoa trung lap (duplicate keys) trong object */
            int count_qid = 0;
            int count_aid = 0;
            for (cJSON *child = item->child; child != NULL; child = child->next) {
                if (child->string != NULL) {
                    if (strcmp(child->string, "question_id") == 0) {
                        count_qid++;
                    } else if (strcmp(child->string, "answer_id") == 0) {
                        count_aid++;
                    }
                }
            }
            if (count_qid != 1 || count_aid != 1) {
                free(sub_answers);
                cJSON_Delete(answers_arr);
                return PRACTICE_ERR_INVALID_PARAM;
            }

            cJSON *q_item = cJSON_GetObjectItemCaseSensitive(item, "question_id");
            cJSON *a_item = cJSON_GetObjectItemCaseSensitive(item, "answer_id");

            if (q_item == NULL || a_item == NULL ||
                !cJSON_IsNumber(q_item) || !cJSON_IsNumber(a_item)) {
                free(sub_answers);
                cJSON_Delete(answers_arr);
                return PRACTICE_ERR_INVALID_PARAM;
            }

            /* Kiem tra ID phai la so nguyen duong, khong phai so thuc va trong pham vi int */
            if (q_item->valuedouble < 1.0 || q_item->valuedouble > (double)INT_MAX ||
                (double)(int)q_item->valuedouble != q_item->valuedouble ||
                a_item->valuedouble < 1.0 || a_item->valuedouble > (double)INT_MAX ||
                (double)(int)a_item->valuedouble != a_item->valuedouble) {
                free(sub_answers);
                cJSON_Delete(answers_arr);
                return PRACTICE_ERR_INVALID_PARAM;
            }

            sub_answers[i].question_id = (int)q_item->valuedouble;
            sub_answers[i].answer_id = (int)a_item->valuedouble;
        }
    }
    cJSON_Delete(answers_arr);

    /* 3. Kiem tra tinh duy nhat cua question_id trong mang client gui (khong cho trung) */
    for (int i = 0; i < num_answers; i++) {
        for (int j = i + 1; j < num_answers; j++) {
            if (sub_answers[i].question_id == sub_answers[j].question_id) {
                free(sub_answers);
                return PRACTICE_ERR_INVALID_ANSWER;
            }
        }
    }

    /* 4. Khoi tao Transaction */
    if (db_begin_transaction(db) != SQLITE_OK) {
        free(sub_answers);
        return PRACTICE_ERR_DATABASE;
    }

    /* 5. Kiem tra attempt_id: ton tai, dung user, dung mode PRACTICE, room_id NULL, chua nop */
    const char *sql_check_attempt =
        "SELECT user_id, room_id, mode, submitted_at FROM exam_attempts WHERE id = ?;";
    sqlite3_stmt *stmt_chk = NULL;
    int rc = sqlite3_prepare_v2(db, sql_check_attempt, -1, &stmt_chk, NULL);
    if (rc != SQLITE_OK || stmt_chk == NULL) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_chk, 1, attempt_id);
    int step_chk = sqlite3_step(stmt_chk);
    if (step_chk == SQLITE_DONE) {
        sqlite3_finalize(stmt_chk);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_NOT_FOUND;
    }
    if (step_chk != SQLITE_ROW) {
        sqlite3_finalize(stmt_chk);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    int db_user_id = sqlite3_column_int(stmt_chk, 0);
    int room_id_is_null = (sqlite3_column_type(stmt_chk, 1) == SQLITE_NULL);
    const unsigned char *db_mode = sqlite3_column_text(stmt_chk, 2);
    int mode_is_practice = (db_mode != NULL && strcmp((const char *)db_mode, "PRACTICE") == 0);
    int submitted_at_is_null = (sqlite3_column_type(stmt_chk, 3) == SQLITE_NULL);
    sqlite3_finalize(stmt_chk);

    if (db_user_id != user_id || !room_id_is_null || !mode_is_practice) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_NOT_FOUND;
    }

    if (!submitted_at_is_null) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_ALREADY_DONE;
    }

    /* 6. Dem so luong cau hoi trong practice_questions cua attempt */
    const char *sql_count_pq = "SELECT COUNT(*) FROM practice_questions WHERE attempt_id = ?;";
    sqlite3_stmt *stmt_cnt = NULL;
    rc = sqlite3_prepare_v2(db, sql_count_pq, -1, &stmt_cnt, NULL);
    if (rc != SQLITE_OK || stmt_cnt == NULL) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_cnt, 1, attempt_id);
    int step_cnt = sqlite3_step(stmt_cnt);
    int total_questions = 0;
    if (step_cnt == SQLITE_ROW) {
        total_questions = sqlite3_column_int(stmt_cnt, 0);
    }
    sqlite3_finalize(stmt_cnt);

    if (total_questions <= 0) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    /* 7. Lay danh sach cau hoi va dap an dung tuong ung theo thu tu position */
    PracticeDetail *details = (PracticeDetail *)malloc(sizeof(PracticeDetail) * (size_t)total_questions);
    if (details == NULL) {
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }
    memset(details, 0, sizeof(PracticeDetail) * (size_t)total_questions);

    const char *sql_select_pq =
        "SELECT pq.question_id, pq.position, "
        "       (SELECT a.id FROM answers a WHERE a.question_id = pq.question_id AND a.is_correct = 1 LIMIT 1) "
        "FROM practice_questions pq "
        "WHERE pq.attempt_id = ? "
        "ORDER BY pq.position ASC;";
    sqlite3_stmt *stmt_pq = NULL;
    rc = sqlite3_prepare_v2(db, sql_select_pq, -1, &stmt_pq, NULL);
    if (rc != SQLITE_OK || stmt_pq == NULL) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    sqlite3_bind_int(stmt_pq, 1, attempt_id);
    int pq_idx = 0;
    int step_pq = SQLITE_DONE;
    while ((step_pq = sqlite3_step(stmt_pq)) == SQLITE_ROW) {
        if (pq_idx < total_questions) {
            details[pq_idx].question_id = sqlite3_column_int(stmt_pq, 0);
            details[pq_idx].position = sqlite3_column_int(stmt_pq, 1);
            details[pq_idx].correct_answer_id = sqlite3_column_int(stmt_pq, 2);
            details[pq_idx].chosen_answer_id = 0;
            details[pq_idx].is_correct = 0;
            pq_idx++;
        }
    }
    sqlite3_finalize(stmt_pq);

    if (step_pq != SQLITE_DONE && step_pq != SQLITE_ROW) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    if (pq_idx != total_questions) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    /* 8. Kiem tra tung dap an client gui voi CSDL */
    const char *sql_chk_ans = "SELECT is_correct FROM answers WHERE id = ? AND question_id = ?;";
    sqlite3_stmt *stmt_ans_chk = NULL;
    rc = sqlite3_prepare_v2(db, sql_chk_ans, -1, &stmt_ans_chk, NULL);
    if (rc != SQLITE_OK || stmt_ans_chk == NULL) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    for (int k = 0; k < num_answers; k++) {
        int q_id = sub_answers[k].question_id;
        int a_id = sub_answers[k].answer_id;

        /* Tim question_id trong bo de practice_questions */
        int found_idx = -1;
        for (int i = 0; i < total_questions; i++) {
            if (details[i].question_id == q_id) {
                found_idx = i;
                break;
            }
        }
        if (found_idx == -1) {
            sqlite3_finalize(stmt_ans_chk);
            free(details);
            free(sub_answers);
            db_rollback(db);
            return PRACTICE_ERR_INVALID_ANSWER;
        }

        /* Xac minh answer_id thuoc question_id */
        sqlite3_bind_int(stmt_ans_chk, 1, a_id);
        sqlite3_bind_int(stmt_ans_chk, 2, q_id);
        int step_ans = sqlite3_step(stmt_ans_chk);
        if (step_ans == SQLITE_ROW) {
            int is_corr = sqlite3_column_int(stmt_ans_chk, 0);
            details[found_idx].chosen_answer_id = a_id;
            details[found_idx].is_correct = (is_corr == 1) ? 1 : 0;
            sqlite3_reset(stmt_ans_chk);
        } else if (step_ans == SQLITE_DONE) {
            sqlite3_finalize(stmt_ans_chk);
            free(details);
            free(sub_answers);
            db_rollback(db);
            return PRACTICE_ERR_INVALID_ANSWER;
        } else {
            sqlite3_finalize(stmt_ans_chk);
            free(details);
            free(sub_answers);
            db_rollback(db);
            return PRACTICE_ERR_DATABASE;
        }
    }
    sqlite3_finalize(stmt_ans_chk);

    /* 9. Luu cac dap an duoc chon vao attempt_answers (bo qua cau bo trong) */
    if (num_answers > 0) {
        const char *sql_ins_ans =
            "INSERT INTO attempt_answers (attempt_id, question_id, answer_id) VALUES (?, ?, ?);";
        sqlite3_stmt *stmt_ins_ans = NULL;
        rc = sqlite3_prepare_v2(db, sql_ins_ans, -1, &stmt_ins_ans, NULL);
        if (rc != SQLITE_OK || stmt_ins_ans == NULL) {
            free(details);
            free(sub_answers);
            db_rollback(db);
            return PRACTICE_ERR_DATABASE;
        }

        for (int k = 0; k < num_answers; k++) {
            sqlite3_bind_int(stmt_ins_ans, 1, attempt_id);
            sqlite3_bind_int(stmt_ins_ans, 2, sub_answers[k].question_id);
            sqlite3_bind_int(stmt_ins_ans, 3, sub_answers[k].answer_id);

            int step_ins = sqlite3_step(stmt_ins_ans);
            sqlite3_reset(stmt_ins_ans);

            if (step_ins != SQLITE_DONE) {
                sqlite3_finalize(stmt_ins_ans);
                free(details);
                free(sub_answers);
                db_rollback(db);
                return PRACTICE_ERR_DATABASE;
            }
        }
        sqlite3_finalize(stmt_ins_ans);
    }

    /* 10. Cham diem: score = 10.0 * correct_count / total_questions */
    int correct_count = 0;
    for (int i = 0; i < total_questions; i++) {
        if (details[i].chosen_answer_id > 0 && details[i].is_correct == 1) {
            correct_count++;
        }
    }
    double score = (10.0 * (double)correct_count) / (double)total_questions;

    /* 11. Cap nhat score va submitted_at vao exam_attempts */
    const char *sql_upd =
        "UPDATE exam_attempts SET score = ?, submitted_at = CURRENT_TIMESTAMP WHERE id = ?;";
    sqlite3_stmt *stmt_upd = NULL;
    rc = sqlite3_prepare_v2(db, sql_upd, -1, &stmt_upd, NULL);
    if (rc != SQLITE_OK || stmt_upd == NULL) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    sqlite3_bind_double(stmt_upd, 1, score);
    sqlite3_bind_int(stmt_upd, 2, attempt_id);

    int step_upd = sqlite3_step(stmt_upd);
    sqlite3_finalize(stmt_upd);

    if (step_upd != SQLITE_DONE) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    /* 12. Xay dung chuoi JSON ket qua */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    cJSON_AddNumberToObject(root, "attempt_id", attempt_id);
    cJSON_AddNumberToObject(root, "total_questions", total_questions);
    cJSON_AddNumberToObject(root, "correct_count", correct_count);
    cJSON_AddNumberToObject(root, "score", score);

    cJSON *details_arr = cJSON_AddArrayToObject(root, "details");
    if (details_arr == NULL) {
        cJSON_Delete(root);
        free(details);
        free(sub_answers);
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    for (int i = 0; i < total_questions; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(root);
            free(details);
            free(sub_answers);
            db_rollback(db);
            return PRACTICE_ERR_MEMORY;
        }
        cJSON_AddItemToArray(details_arr, item);

        cJSON_AddNumberToObject(item, "question_id", details[i].question_id);
        if (details[i].chosen_answer_id > 0) {
            cJSON_AddNumberToObject(item, "chosen_answer_id", details[i].chosen_answer_id);
        } else {
            cJSON_AddNullToObject(item, "chosen_answer_id");
        }
        cJSON_AddNumberToObject(item, "correct_answer_id", details[i].correct_answer_id);
        cJSON_AddBoolToObject(item, "is_correct", details[i].is_correct ? 1 : 0);
    }

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    free(details);
    free(sub_answers);

    if (json_out == NULL) {
        db_rollback(db);
        return PRACTICE_ERR_MEMORY;
    }

    /* 13. Commit Transaction */
    if (db_commit(db) != SQLITE_OK) {
        free(json_out);
        db_rollback(db);
        return PRACTICE_ERR_DATABASE;
    }

    *out_result_json = json_out;
    return PRACTICE_SUCCESS;
}

