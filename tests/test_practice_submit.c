#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>
#include <math.h>

#include "practice.h"

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
    } \
} while (0)

static char *read_file_to_string(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }

    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }

    size_t read_bytes = fread(buf, 1, (size_t)size, f);
    buf[read_bytes] = '\0';
    fclose(f);
    return buf;
}

static void exec_sql(sqlite3 *db, const char *sql)
{
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL Error: %s\nSQL: %s\n", err_msg ? err_msg : "Unknown", sql);
        sqlite3_free(err_msg);
        exit(EXIT_FAILURE);
    }
}

static int count_rows(sqlite3 *db, const char *table)
{
    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        return -1;
    }

    int count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

static sqlite3 *setup_test_db(void)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK || !db) {
        fprintf(stderr, "Cannot open in-memory database\n");
        if (db) {
            sqlite3_close(db);
        }
        return NULL;
    }

    exec_sql(db, "PRAGMA foreign_keys = ON;");

    char *schema = read_file_to_string("database/schema.sql");
    if (!schema) {
        fprintf(stderr, "Failed to read database/schema.sql\n");
        sqlite3_close(db);
        return NULL;
    }

    exec_sql(db, schema);
    free(schema);

    return db;
}

/*
 * Fixture:
 * - 2 users: id 1 (alice), id 2 (bob)
 * - 4 cau hoi hop le (de co the tao attempt 4 cau hoac 2 cau):
 *   + q1: Net, ans 1 (corr), ans 2 (wrong)
 *   + q2: Net, ans 3 (corr), ans 4 (wrong)
 *   + q3: Net, ans 5 (corr), ans 6 (wrong)
 *   + q4: Net, ans 7 (corr), ans 8 (wrong)
 * - 1 exam room de tao attempt phong thi mode EXAM
 */
static void seed_test_data(sqlite3 *db)
{
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (1, 'alice', 'hash1');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (2, 'bob', 'hash2');");

    exec_sql(db, "INSERT INTO rooms (id, owner_id, name, question_count, duration_minutes) "
                 "VALUES (10, 1, 'Exam Room 1', 10, 60);");

    /* q1 */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (1, 'Q1 Net', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (1, 1, 'A1-Correct', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (2, 1, 'A1-Wrong', 0);");

    /* q2 */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (2, 'Q2 Net', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (3, 2, 'A2-Correct', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (4, 2, 'A2-Wrong', 0);");

    /* q3 */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (3, 'Q3 Net', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (5, 3, 'A3-Correct', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (6, 3, 'A3-Wrong', 0);");

    /* q4 */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (4, 'Q4 Net', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (7, 4, 'A4-Correct', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (8, 4, 'A4-Wrong', 0);");
}

/* =========================================================================
 * NHÓM A — NỘP BÀI THÀNH CÔNG (Tests 1 - 13)
 * ========================================================================= */
static void test_group_a_success(sqlite3 *db)
{
    printf("\n--- Nhóm A: Nộp bài thành công (Tests 1 - 13) ---\n");

    /* 1. Nộp tất cả câu đúng -> score = 10.0 */
    char *start_json = NULL;
    int att_all_corr = practice_start(db, 1, "Network", "EASY", 2, &start_json);
    TEST_ASSERT(att_all_corr > 0 && start_json != NULL, "Tạo attempt cho test 1 thành công");

    /* Parse start_json de biet 2 question_id duoc chon */
    cJSON *pj = cJSON_Parse(start_json);
    cJSON *qs = cJSON_GetObjectItemCaseSensitive(pj, "questions");
    int q_id_1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 0), "question_id")->valueint;
    int q_id_2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 1), "question_id")->valueint;
    cJSON_Delete(pj);
    free(start_json);

    /* Dap an dung tuong ung: q1->1, q2->3, q3->5, q4->7 */
    int ans_corr_1 = (q_id_1 * 2) - 1;
    int ans_corr_2 = (q_id_2 * 2) - 1;
    int ans_wrong_1 = q_id_1 * 2;
    int ans_wrong_2 = q_id_2 * 2;

    char payload[256];
    snprintf(payload, sizeof(payload),
             "[{\"question_id\": %d, \"answer_id\": %d}, {\"question_id\": %d, \"answer_id\": %d}]",
             q_id_1, ans_corr_1, q_id_2, ans_corr_2);

    char *res_json = NULL;
    int rc = practice_submit(db, att_all_corr, 1, payload, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS, "1a. practice_submit trả về PRACTICE_SUCCESS khi nộp tất cả câu đúng");
    TEST_ASSERT(res_json != NULL, "1b. res_json không NULL khi nộp thành công");

    cJSON *res_obj = cJSON_Parse(res_json);
    double score1 = cJSON_GetObjectItemCaseSensitive(res_obj, "score")->valuedouble;
    TEST_ASSERT(fabs(score1 - 10.0) < 0.0001, "1c. Điểm nộp tất cả câu đúng bằng đúng 10.0");
    cJSON_Delete(res_obj);
    free(res_json);

    /* 2. Nộp tất cả câu sai -> score = 0.0 */
    start_json = NULL;
    int att_all_wrong = practice_start(db, 1, "Network", "EASY", 2, &start_json);
    pj = cJSON_Parse(start_json);
    qs = cJSON_GetObjectItemCaseSensitive(pj, "questions");
    q_id_1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 0), "question_id")->valueint;
    q_id_2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 1), "question_id")->valueint;
    cJSON_Delete(pj);
    free(start_json);

    ans_wrong_1 = q_id_1 * 2;
    ans_wrong_2 = q_id_2 * 2;
    snprintf(payload, sizeof(payload),
             "[{\"question_id\": %d, \"answer_id\": %d}, {\"question_id\": %d, \"answer_id\": %d}]",
             q_id_1, ans_wrong_1, q_id_2, ans_wrong_2);

    res_json = NULL;
    rc = practice_submit(db, att_all_wrong, 1, payload, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS && res_json != NULL, "2a. Nộp tất cả câu sai thành công");
    res_obj = cJSON_Parse(res_json);
    double score2 = cJSON_GetObjectItemCaseSensitive(res_obj, "score")->valuedouble;
    TEST_ASSERT(fabs(score2 - 0.0) < 0.0001, "2b. Điểm nộp tất cả câu sai bằng đúng 0.0");
    cJSON_Delete(res_obj);
    free(res_json);

    /* 3. Nộp một phần đúng, một phần sai -> score = 5.0 (1 dung, 1 sai / 2 cau) */
    start_json = NULL;
    int att_half = practice_start(db, 1, "Network", "EASY", 2, &start_json);
    pj = cJSON_Parse(start_json);
    qs = cJSON_GetObjectItemCaseSensitive(pj, "questions");
    q_id_1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 0), "question_id")->valueint;
    q_id_2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 1), "question_id")->valueint;
    cJSON_Delete(pj);
    free(start_json);

    ans_corr_1 = (q_id_1 * 2) - 1;
    ans_wrong_2 = q_id_2 * 2;
    snprintf(payload, sizeof(payload),
             "[{\"question_id\": %d, \"answer_id\": %d}, {\"question_id\": %d, \"answer_id\": %d}]",
             q_id_1, ans_corr_1, q_id_2, ans_wrong_2);

    res_json = NULL;
    rc = practice_submit(db, att_half, 1, payload, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS, "3a. Nộp một phần đúng, một phần sai thành công");
    res_obj = cJSON_Parse(res_json);
    double score3 = cJSON_GetObjectItemCaseSensitive(res_obj, "score")->valuedouble;
    TEST_ASSERT(fabs(score3 - 5.0) < 0.0001, "3b. Điểm 1 đúng / 2 câu bằng đúng 5.0");
    cJSON_Delete(res_obj);
    free(res_json);

    /* 4. Bỏ trống một số câu -> câu bỏ trống tính sai (1 câu đúng, 1 câu bỏ trống / 2 câu -> score = 5.0) */
    start_json = NULL;
    int att_blank_some = practice_start(db, 1, "Network", "EASY", 2, &start_json);
    pj = cJSON_Parse(start_json);
    qs = cJSON_GetObjectItemCaseSensitive(pj, "questions");
    q_id_1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 0), "question_id")->valueint;
    q_id_2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(qs, 1), "question_id")->valueint;
    cJSON_Delete(pj);
    free(start_json);

    ans_corr_1 = (q_id_1 * 2) - 1;
    snprintf(payload, sizeof(payload),
             "[{\"question_id\": %d, \"answer_id\": %d}]", q_id_1, ans_corr_1);

    res_json = NULL;
    rc = practice_submit(db, att_blank_some, 1, payload, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS, "4a. Nộp bài có câu bỏ trống thành công");
    res_obj = cJSON_Parse(res_json);
    double score4 = cJSON_GetObjectItemCaseSensitive(res_obj, "score")->valuedouble;
    TEST_ASSERT(fabs(score4 - 5.0) < 0.0001, "4b. Câu bỏ trống bị tính sai, điểm đạt 5.0");

    /* 7, 8, 9, 10: Kiểm tra chi tiết details */
    cJSON *details = cJSON_GetObjectItemCaseSensitive(res_obj, "details");
    TEST_ASSERT(cJSON_IsArray(details), "7a. details trong JSON là mảng");
    TEST_ASSERT(cJSON_GetArraySize(details) == 2, "7b. details chứa đủ 2 câu hỏi của attempt");

    cJSON *d0 = cJSON_GetArrayItem(details, 0);
    cJSON *d1 = cJSON_GetArrayItem(details, 1);
    TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d0, "question_id")->valueint == q_id_1, "8a. details[0] đúng position 1");
    TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d1, "question_id")->valueint == q_id_2, "8b. details[1] đúng position 2");

    TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d0, "chosen_answer_id")->valueint == ans_corr_1, "details[0] có chosen_answer_id");
    TEST_ASSERT(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(d1, "chosen_answer_id")), "9. chosen_answer_id = null với câu bỏ trống");

    int corr_in_db_1 = (q_id_1 * 2) - 1;
    int corr_in_db_2 = (q_id_2 * 2) - 1;
    TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d0, "correct_answer_id")->valueint == corr_in_db_1, "10a. correct_answer_id khớp CSDL câu 1");
    TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d1, "correct_answer_id")->valueint == corr_in_db_2, "10b. correct_answer_id khớp CSDL câu 2");

    cJSON_Delete(res_obj);
    free(res_json);

    /* 5. Nộp mảng rỗng [] -> score = 0 */
    start_json = NULL;
    int att_empty = practice_start(db, 1, "Network", "EASY", 2, &start_json);
    free(start_json);

    res_json = NULL;
    rc = practice_submit(db, att_empty, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS, "5a. Nộp mảng rỗng [] thành công");
    res_obj = cJSON_Parse(res_json);
    double score5 = cJSON_GetObjectItemCaseSensitive(res_obj, "score")->valuedouble;
    TEST_ASSERT(fabs(score5 - 0.0) < 0.0001, "5b. Nộp mảng rỗng có score = 0.0");

    /* 6. correct_count và total_questions chính xác */
    int cnt_c = cJSON_GetObjectItemCaseSensitive(res_obj, "correct_count")->valueint;
    int total_q = cJSON_GetObjectItemCaseSensitive(res_obj, "total_questions")->valueint;
    TEST_ASSERT(cnt_c == 0, "6a. correct_count chính xác (0)");
    TEST_ASSERT(total_q == 2, "6b. total_questions chính xác (2)");
    cJSON_Delete(res_obj);
    free(res_json);

    /* 11, 12, 13. Kiểm tra CSDL sau khi submit att_blank_some */
    /* att_blank_some nộp 1 câu, bỏ trống 1 câu, score = 5.0 */
    sqlite3_stmt *stmt = NULL;
    const char *sql_chk_db = "SELECT score, submitted_at FROM exam_attempts WHERE id = ?;";
    rc = sqlite3_prepare_v2(db, sql_chk_db, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuẩn bị statement kiểm tra exam_attempts");

    double db_score = -1.0;
    int submitted_at_not_null = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, att_blank_some);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            db_score = sqlite3_column_double(stmt, 0);
            submitted_at_not_null = (sqlite3_column_type(stmt, 1) != SQLITE_NULL);
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(submitted_at_not_null, "11. submitted_at trong CSDL đã được cập nhật khác NULL");
    TEST_ASSERT(fabs(db_score - 5.0) < 0.0001, "12. score trong database (5.0) khớp với JSON");

    /* 13. attempt_answers chỉ chứa câu đã trả lời (1 câu) */
    const char *sql_cnt_ans = "SELECT COUNT(*) FROM attempt_answers WHERE attempt_id = ?;";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_cnt_ans, -1, &stmt, NULL);
    int ans_rows = -1;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, att_blank_some);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            ans_rows = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(ans_rows == 1, "13. attempt_answers chỉ chứa 1 bản ghi câu đã trả lời, không lưu câu bỏ trống");
}

/* =========================================================================
 * NHÓM B — QUYỀN TRUY CẬP VÀ TRẠNG THÁI (Tests 14 - 19)
 * ========================================================================= */
static void test_group_b_access_and_state(sqlite3 *db)
{
    printf("\n--- Nhóm B: Quyền truy cập và trạng thái (Tests 14 - 19) ---\n");

    /* Tạo 1 attempt hoàn thành bởi alice (user_id = 1) */
    char *start_json = NULL;
    int att_done = practice_start(db, 1, "Network", "EASY", 1, &start_json);
    free(start_json);

    char *res_json = NULL;
    int rc = practice_submit(db, att_done, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS, "Nộp lần đầu thành công");
    free(res_json);

    /* 14. Nộp lại attempt đã hoàn thành -> PRACTICE_ERR_ALREADY_DONE */
    res_json = (char *)0x12;
    rc = practice_submit(db, att_done, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_ALREADY_DONE, "14a. Nộp lại attempt đã nộp trả về PRACTICE_ERR_ALREADY_DONE");
    TEST_ASSERT(res_json == NULL, "14b. *out_result_json là NULL khi nộp lại");

    /* 15. User khác (bob = 2) nộp attempt chưa nộp của alice -> PRACTICE_ERR_NOT_FOUND */
    start_json = NULL;
    int att_alice_new = practice_start(db, 1, "Network", "EASY", 1, &start_json);
    free(start_json);

    res_json = (char *)0x12;
    rc = practice_submit(db, att_alice_new, 2, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "15. User khác nộp attempt của alice trả về PRACTICE_ERR_NOT_FOUND");
    TEST_ASSERT(res_json == NULL, "*out_result_json là NULL");

    /* 16. Attempt không tồn tại -> PRACTICE_ERR_NOT_FOUND */
    res_json = (char *)0x12;
    rc = practice_submit(db, 99999, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "16. Attempt 99999 không tồn tại trả về PRACTICE_ERR_NOT_FOUND");
    TEST_ASSERT(res_json == NULL, "*out_result_json là NULL");

    /* 17. Attempt có mode = 'EXAM' -> bị từ chối với PRACTICE_ERR_NOT_FOUND */
    exec_sql(db, "INSERT INTO exam_attempts (id, room_id, user_id, mode) VALUES (500, NULL, 1, 'EXAM');");
    exec_sql(db, "INSERT INTO practice_questions (attempt_id, question_id, position) VALUES (500, 1, 1);");
    res_json = (char *)0x12;
    rc = practice_submit(db, 500, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "17. Attempt có mode = 'EXAM' bị từ chối với PRACTICE_ERR_NOT_FOUND");
    TEST_ASSERT(res_json == NULL, "*out_result_json là NULL");

    /* 18. Attempt có room_id != NULL -> bị từ chối với PRACTICE_ERR_NOT_FOUND */
    exec_sql(db, "INSERT INTO exam_attempts (id, room_id, user_id, mode) VALUES (501, 10, 1, 'PRACTICE');");
    exec_sql(db, "INSERT INTO practice_questions (attempt_id, question_id, position) VALUES (501, 1, 1);");
    res_json = (char *)0x12;
    rc = practice_submit(db, 501, 1, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "18. Attempt có room_id != NULL bị từ chối với PRACTICE_ERR_NOT_FOUND");
    TEST_ASSERT(res_json == NULL, "*out_result_json là NULL");

    /* 19. Không tiết lộ trạng thái đã nộp của attempt thuộc user khác
     * (att_done của alice đã nộp xong, bob nộp thử -> vẫn phải là PRACTICE_ERR_NOT_FOUND chứ không phải ALREADY_DONE) */
    res_json = (char *)0x12;
    rc = practice_submit(db, att_done, 2, "[]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "19. Bob nộp attempt đã xong của Alice vẫn nhận PRACTICE_ERR_NOT_FOUND (không lộ thông tin)");
    TEST_ASSERT(res_json == NULL, "*out_result_json là NULL");
}

/* =========================================================================
 * NHÓM C — VALIDATION JSON (Tests 20 - 37)
 * ========================================================================= */
static void test_group_c_json_validation(sqlite3 *db)
{
    printf("\n--- Nhóm C: Validation JSON (Tests 20 - 37) ---\n");

    char *start_json = NULL;
    int att_id = practice_start(db, 1, "Network", "EASY", 1, &start_json);
    free(start_json);

    char *out_res = (char *)0x1234;

    /* 20. db == NULL */
    int rc = practice_submit(NULL, att_id, 1, "[]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM, "20. db == NULL trả về PRACTICE_ERR_INVALID_PARAM");

    /* 21. out_result_json == NULL */
    rc = practice_submit(db, att_id, 1, "[]", NULL);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM, "21. out_result_json == NULL trả về PRACTICE_ERR_INVALID_PARAM");

    /* 22. answers_json == NULL */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, NULL, &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "22. answers_json == NULL trả về PRACTICE_ERR_INVALID_PARAM");

    /* 23. attempt_id <= 0 */
    out_res = (char *)0x1234;
    rc = practice_submit(db, 0, 1, "[]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "23a. attempt_id = 0 trả về PRACTICE_ERR_INVALID_PARAM");
    out_res = (char *)0x1234;
    rc = practice_submit(db, -1, 1, "[]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "23b. attempt_id < 0 trả về PRACTICE_ERR_INVALID_PARAM");

    /* 24. user_id <= 0 */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 0, "[]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "24a. user_id = 0 trả về PRACTICE_ERR_INVALID_PARAM");
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, -5, "[]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "24b. user_id < 0 trả về PRACTICE_ERR_INVALID_PARAM");

    /* 25. JSON sai cú pháp */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "{invalid_json", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "25. JSON sai cú pháp trả về PRACTICE_ERR_INVALID_PARAM");

    /* 26. JSON hợp lệ nhưng không phải array (ví dụ object {}, string "abc", number 123) */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "{}", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "26a. JSON object trả về PRACTICE_ERR_INVALID_PARAM");
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "\"hello\"", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "26b. JSON string trả về PRACTICE_ERR_INVALID_PARAM");

    /* 27. Phần tử array không phải object (ví dụ [1, 2] hoặc ["str"]) */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[123]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "27. Phần tử mảng là number trả về PRACTICE_ERR_INVALID_PARAM");

    /* 28. Thiếu question_id */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "28. Thiếu question_id trả về PRACTICE_ERR_INVALID_PARAM");

    /* 29. Thiếu answer_id */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "29. Thiếu answer_id trả về PRACTICE_ERR_INVALID_PARAM");

    /* 30. ID là chuỗi */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": \"1\", \"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "30. ID là chuỗi trả về PRACTICE_ERR_INVALID_PARAM");

    /* 31. ID là số thực */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": 1.5, \"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "31. ID là số thực trả về PRACTICE_ERR_INVALID_PARAM");

    /* 32. ID là boolean */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": true, \"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "32. ID là boolean trả về PRACTICE_ERR_INVALID_PARAM");

    /* 33. ID bằng 0 hoặc âm */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": 0, \"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "33a. question_id = 0 trả về PRACTICE_ERR_INVALID_PARAM");
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": 1, \"answer_id\": -2}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "33b. answer_id < 0 trả về PRACTICE_ERR_INVALID_PARAM");

    /* 34. ID vượt INT_MAX */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[{\"question_id\": 3000000000, \"answer_id\": 1}]", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "34. question_id vượt INT_MAX trả về PRACTICE_ERR_INVALID_PARAM");

    /* 35. JSON có ký tự thừa sau mảng */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1, "[] garbage_text", &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "35. JSON có ký tự thừa sau mảng trả về PRACTICE_ERR_INVALID_PARAM");

    /* 36. Hai phần tử có cùng question_id */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1,
                         "[{\"question_id\": 1, \"answer_id\": 1}, {\"question_id\": 1, \"answer_id\": 2}]",
                         &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && out_res == NULL, "36. Hai phần tử cùng question_id trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 37. JSON object có khóa question_id hoặc answer_id trùng nhau */
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1,
                         "[{\"question_id\": 1, \"question_id\": 2, \"answer_id\": 1}]",
                         &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "37a. Object có khóa question_id trùng nhau trả về PRACTICE_ERR_INVALID_PARAM");
    out_res = (char *)0x1234;
    rc = practice_submit(db, att_id, 1,
                         "[{\"question_id\": 1, \"answer_id\": 1, \"answer_id\": 2}]",
                         &out_res);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_res == NULL, "37b. Object có khóa answer_id trùng nhau trả về PRACTICE_ERR_INVALID_PARAM");
}

/* =========================================================================
 * NHÓM D — KIỂM TRA NGHIỆP VỤ (Tests 38 - 43)
 * ========================================================================= */
static void test_group_d_business_rules(sqlite3 *db)
{
    printf("\n--- Nhóm D: Kiểm tra nghiệp vụ (Tests 38 - 43) ---\n");

    /* Tạo attempt 1 chỉ có 1 câu hỏi (q1) */
    char *start_json1 = NULL;
    int att_1 = practice_start(db, 1, "Network", "EASY", 1, &start_json1);
    cJSON *pj1 = cJSON_Parse(start_json1);
    int q_att1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pj1, "questions"), 0), "question_id")->valueint;
    cJSON_Delete(pj1);
    free(start_json1);

    /* Tạo attempt 2 của bob có câu hỏi khác */
    char *start_json2 = NULL;
    int att_2 = practice_start(db, 2, "Network", "EASY", 1, &start_json2);
    free(start_json2);

    char *res_json = (char *)0x1234;

    /* 38. question_id không thuộc attempt (ví dụ q = 999) */
    int rc = practice_submit(db, att_1, 1, "[{\"question_id\": 999, \"answer_id\": 1}]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && res_json == NULL, "38. question_id 999 không thuộc attempt trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 39. answer_id không tồn tại (ví dụ answer_id = 99999) */
    res_json = (char *)0x1234;
    char payload39[128];
    snprintf(payload39, sizeof(payload39), "[{\"question_id\": %d, \"answer_id\": 99999}]", q_att1);
    rc = practice_submit(db, att_1, 1, payload39, &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && res_json == NULL, "39. answer_id không tồn tại trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 40. answer_id thuộc câu hỏi khác (q1 nhận answer của q2) */
    /* ans 4 thuộc q2, không thuộc q1 */
    res_json = (char *)0x1234;
    rc = practice_submit(db, att_1, 1, "[{\"question_id\": 1, \"answer_id\": 4}]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && res_json == NULL, "40. answer_id thuộc câu hỏi khác trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 41. Câu hỏi thuộc attempt khác: tìm 1 câu hỏi hợp lệ trong CSDL nhưng KHÔNG thuộc att_1 */
    int other_q = (q_att1 == 1) ? 2 : 1;
    int other_a = (other_q * 2);
    res_json = (char *)0x1234;
    char payload41[128];
    snprintf(payload41, sizeof(payload41), "[{\"question_id\": %d, \"answer_id\": %d}]", other_q, other_a);
    rc = practice_submit(db, att_1, 1, payload41, &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && res_json == NULL, "41. Câu hỏi hợp lệ trong CSDL nhưng ngoài attempt trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 42. Một đáp án hợp lệ đi kèm một đáp án không hợp lệ trong cùng payload */
    int valid_a = (q_att1 * 2);
    res_json = (char *)0x1234;
    char payload42[256];
    snprintf(payload42, sizeof(payload42),
             "[{\"question_id\": %d, \"answer_id\": %d}, {\"question_id\": 999, \"answer_id\": 1}]",
             q_att1, valid_a);
    rc = practice_submit(db, att_1, 1, payload42, &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER && res_json == NULL, "42. 1 hợp lệ + 1 không hợp lệ trong payload trả về PRACTICE_ERR_INVALID_ANSWER");

    /* 43. Không được ghi dữ liệu một phần nếu có lỗi */
    const char *sql_cnt_att1 = "SELECT COUNT(*) FROM attempt_answers WHERE attempt_id = ?;";
    sqlite3_stmt *stmt43 = NULL;
    sqlite3_prepare_v2(db, sql_cnt_att1, -1, &stmt43, NULL);
    sqlite3_bind_int(stmt43, 1, att_1);
    int aa_att1 = 0;
    if (sqlite3_step(stmt43) == SQLITE_ROW) {
        aa_att1 = sqlite3_column_int(stmt43, 0);
    }
    sqlite3_finalize(stmt43);
    TEST_ASSERT(aa_att1 == 0, "43. attempt_answers không bị ghi bản ghi dở dang nào cho attempt_id bị lỗi");

    (void)att_2;
}

/* =========================================================================
 * NHÓM E — TÍNH TOÀN VẸN VÀ PRAGMA (Tests 44 - 49)
 * ========================================================================= */
static void test_group_e_integrity(sqlite3 *db)
{
    printf("\n--- Nhóm E: Tính toàn vẹn và PRAGMA (Tests 44 - 49) ---\n");

    /* Tạo 1 attempt mới để thử lỗi */
    char *start_json = NULL;
    int att_err = practice_start(db, 1, "Network", "EASY", 1, &start_json);
    free(start_json);

    int aa_before = count_rows(db, "attempt_answers");

    /* Gây lỗi bằng payload câu hỏi ngoài bộ đề */
    char *res_json = (char *)0xDEADBEEF;
    int rc = practice_submit(db, att_err, 1, "[{\"question_id\": 9999, \"answer_id\": 1}]", &res_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_ANSWER, "Submit thất bại như dự kiến");

    /* 44. Sau lỗi, *out_result_json == NULL */
    TEST_ASSERT(res_json == NULL, "44. Sau lỗi, *out_result_json == NULL");

    /* Kiểm tra trạng thái attempt trong CSDL */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT score, submitted_at FROM exam_attempts WHERE id = ?;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuẩn bị statement kiểm tra tính toàn vẹn attempt");

    int score_is_null = 0;
    int sub_at_is_null = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, att_err);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            score_is_null = (sqlite3_column_type(stmt, 0) == SQLITE_NULL);
            sub_at_is_null = (sqlite3_column_type(stmt, 1) == SQLITE_NULL);
        }
        sqlite3_finalize(stmt);
    }

    /* 45. Sau lỗi, submitted_at vẫn NULL */
    TEST_ASSERT(sub_at_is_null, "45. Sau lỗi, submitted_at của attempt vẫn là NULL");

    /* 46. Sau lỗi, score vẫn NULL */
    TEST_ASSERT(score_is_null, "46. Sau lỗi, score của attempt vẫn là NULL");

    /* 47. Sau lỗi, attempt_answers không bị ghi dở dang */
    int aa_after = count_rows(db, "attempt_answers");
    TEST_ASSERT(aa_before == aa_after, "47. Sau lỗi, attempt_answers không bị thay đổi");

    /* 48. PRAGMA integrity_check trả về ok */
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, NULL);
    int integrity_ok = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *txt = sqlite3_column_text(stmt, 0);
            if (txt && strcmp((const char *)txt, "ok") == 0) {
                integrity_ok = 1;
            }
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(integrity_ok, "48. PRAGMA integrity_check trả về 'ok'");

    /* 49. PRAGMA foreign_key_check không có vi phạm */
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
    int fk_violations = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fk_violations++;
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(fk_violations == 0, "49. PRAGMA foreign_key_check không có vi phạm");
}

/* =========================================================================
 * MỤC 3 — KIỂM THỬ TRƯỜNG HỢP DỮ LIỆU THAY ĐỔI
 * ========================================================================= */
static void test_section_3_data_mutation(sqlite3 *db)
{
    printf("\n--- Mục 3: Kiểm thử trường hợp dữ liệu thay đổi ---\n");

    /* Trường hợp 3.1: Đáp án đúng bị sửa sau khi start nhưng trước khi submit */
    char *start_json = NULL;
    int att_mut = practice_start(db, 1, "Network", "EASY", 1, &start_json);
    TEST_ASSERT(att_mut > 0 && start_json != NULL, "Tạo attempt cho trường hợp 3.1 thành công");

    cJSON *pj1 = cJSON_Parse(start_json);
    int q_id_mut = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pj1, "questions"), 0), "question_id")->valueint;
    cJSON_Delete(pj1);
    free(start_json);

    int ans_init_corr = (q_id_mut * 2) - 1;
    int ans_init_wrong = q_id_mut * 2;

    /* Admin đảo đáp án đúng: đáp án đúng ban đầu thành 0, đáp án sai ban đầu thành 1 */
    char sql_upd[128];
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 0 WHERE id = %d;", ans_init_corr);
    exec_sql(db, sql_upd);
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 1 WHERE id = %d;", ans_init_wrong);
    exec_sql(db, sql_upd);

    /* Thí sinh chọn ans_init_wrong (đáp án đúng mới) */
    char payload31[128];
    snprintf(payload31, sizeof(payload31), "[{\"question_id\": %d, \"answer_id\": %d}]", q_id_mut, ans_init_wrong);

    char *res_json = NULL;
    int rc = practice_submit(db, att_mut, 1, payload31, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS && res_json != NULL, "3.1a. Nộp bài sau khi đáp án bị sửa thành công");
    if (res_json) {
        cJSON *r = cJSON_Parse(res_json);
        double sc = cJSON_GetObjectItemCaseSensitive(r, "score")->valuedouble;
        TEST_ASSERT(fabs(sc - 10.0) < 0.0001, "3.1b. Điểm được tính theo đáp án đúng mới nhất tại thời điểm submit (10.0)");
        cJSON *d = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "details"), 0);
        TEST_ASSERT(cJSON_GetObjectItemCaseSensitive(d, "correct_answer_id")->valueint == ans_init_wrong, "3.1c. correct_answer_id cập nhật thành đáp án đúng mới");
        cJSON_Delete(r);
        free(res_json);
    }

    /* Khôi phục lại */
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 1 WHERE id = %d;", ans_init_corr);
    exec_sql(db, sql_upd);
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 0 WHERE id = %d;", ans_init_wrong);
    exec_sql(db, sql_upd);

    /* Trường hợp 3.2: Một câu hỏi có nhiều hơn 1 đáp án đúng do dữ liệu thay đổi */
    char *start_json2 = NULL;
    int att_multi = practice_start(db, 1, "Network", "EASY", 1, &start_json2);
    TEST_ASSERT(att_multi > 0 && start_json2 != NULL, "Tạo attempt cho trường hợp 3.2 thành công");

    cJSON *pj2 = cJSON_Parse(start_json2);
    int q_id_multi = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pj2, "questions"), 0), "question_id")->valueint;
    cJSON_Delete(pj2);
    free(start_json2);

    int ans_corr_b = q_id_multi * 2;

    /* Đặt cả hai đáp án đều là đúng (is_correct = 1) */
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 1 WHERE id = %d;", ans_corr_b);
    exec_sql(db, sql_upd);

    /* Thí sinh chọn đáp án ans_corr_b */
    char payload32[128];
    snprintf(payload32, sizeof(payload32), "[{\"question_id\": %d, \"answer_id\": %d}]", q_id_multi, ans_corr_b);

    res_json = NULL;
    rc = practice_submit(db, att_multi, 1, payload32, &res_json);
    TEST_ASSERT(rc == PRACTICE_SUCCESS && res_json != NULL, "3.2a. Nộp bài với câu hỏi có 2 đáp án đúng thành công");
    if (res_json) {
        cJSON *r = cJSON_Parse(res_json);
        double sc = cJSON_GetObjectItemCaseSensitive(r, "score")->valuedouble;
        TEST_ASSERT(fabs(sc - 10.0) < 0.0001, "3.2b. Thí sinh chọn 1 trong 2 đáp án đúng vẫn được tính điểm đúng (10.0)");
        cJSON_Delete(r);
        free(res_json);
    }

    /* Khôi phục lại */
    snprintf(sql_upd, sizeof(sql_upd), "UPDATE answers SET is_correct = 0 WHERE id = %d;", ans_corr_b);
    exec_sql(db, sql_upd);
}

int main(void)
{
    printf("================================================================\n");
    printf("RUNNING COMPREHENSIVE TESTS FOR PRACTICE_SUBMIT (TASK 04.5.1)\n");
    printf("================================================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "FATAL: Could not initialize SQLite test database.\n");
        return EXIT_FAILURE;
    }

    seed_test_data(db);

    test_group_a_success(db);
    test_group_b_access_and_state(db);
    test_group_c_json_validation(db);
    test_group_d_business_rules(db);
    test_group_e_integrity(db);
    test_section_3_data_mutation(db);

    sqlite3_close(db);

    printf("\n================================================================\n");
    printf("PRACTICE_SUBMIT TEST RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    if (g_tests_passed == g_tests_run && g_tests_run > 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}
