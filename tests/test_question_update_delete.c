#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "question.h"

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
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t rb = fread(buf, 1, (size_t)size, f);
    buf[rb] = '\0';
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

static int count_table_rows(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return -1;
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
        if (db) sqlite3_close(db);
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

static const char *FIXTURE_ANSWERS_2 =
    "["
    "  {\"content\": \"Transport Layer\", \"is_correct\": 1},"
    "  {\"content\": \"Network Layer\", \"is_correct\": 0}"
    "]";

static const char *FIXTURE_ANSWERS_4 =
    "["
    "  {\"content\": \"Transport Layer (Tầng 4)\", \"is_correct\": 1},"
    "  {\"content\": \"Network Layer (Tầng 3)\", \"is_correct\": 0},"
    "  {\"content\": \"Data Link Layer (Tầng 2)\", \"is_correct\": 0},"
    "  {\"content\": \"Application Layer (Tầng 7)\", \"is_correct\": 0}"
    "]";

/* =========================================================================
 * NHÓM A — UPDATE THÔNG TIN
 * ========================================================================= */
static void test_group_a_update_info(void)
{
    printf("\n--- Nhóm A: Update thông tin câu hỏi ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0;
    int rc = question_create(db, "TCP là giao thức gì?", "Network", "EASY", FIXTURE_ANSWERS_2, &q1);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q1 > 0, "Tạo câu hỏi q1 thành công");

    /* Lấy thông tin ban đầu */
    char *det_init = NULL;
    question_get_detail(db, q1, &det_init);
    cJSON *r_init = cJSON_Parse(det_init);
    char orig_created_at[128];
    strncpy(orig_created_at, cJSON_GetObjectItemCaseSensitive(r_init, "created_at")->valuestring, sizeof(orig_created_at) - 1);
    cJSON *init_answers = cJSON_GetObjectItemCaseSensitive(r_init, "answers");
    int orig_a1_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(init_answers, 0), "answer_id")->valueint;
    int orig_a2_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(init_answers, 1), "answer_id")->valueint;
    cJSON_Delete(r_init);
    free(det_init);

    /* 1. Cập nhật content, topic, difficulty thành công, answers_json == NULL */
    rc = question_update(db, q1, "Giao thức TCP hoạt động ở tầng nào?", "Computer Network", "MEDIUM", NULL);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "A1. question_update() thành công khi answers_json == NULL");

    char *det_after = NULL;
    rc = question_get_detail(db, q1, &det_after);
    TEST_ASSERT(rc == QUESTION_SUCCESS && det_after != NULL, "A6a. question_get_detail() thành công sau cập nhật");

    if (det_after) {
        cJSON *root = cJSON_Parse(det_after);
        cJSON *qid = cJSON_GetObjectItemCaseSensitive(root, "question_id");
        cJSON *content = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *topic = cJSON_GetObjectItemCaseSensitive(root, "topic");
        cJSON *difficulty = cJSON_GetObjectItemCaseSensitive(root, "difficulty");
        cJSON *created_at = cJSON_GetObjectItemCaseSensitive(root, "created_at");
        cJSON *answers = cJSON_GetObjectItemCaseSensitive(root, "answers");

        TEST_ASSERT(qid && qid->valueint == q1, "A4. question_id không thay đổi");
        TEST_ASSERT(content && strcmp(content->valuestring, "Giao thức TCP hoạt động ở tầng nào?") == 0,
                    "A6b. content phản ánh đúng giá trị mới");
        TEST_ASSERT(topic && strcmp(topic->valuestring, "Computer Network") == 0,
                    "A6c. topic phản ánh đúng giá trị mới");
        TEST_ASSERT(difficulty && strcmp(difficulty->valuestring, "MEDIUM") == 0,
                    "A6d. difficulty phản ánh đúng giá trị mới");
        TEST_ASSERT(created_at && strcmp(created_at->valuestring, orig_created_at) == 0,
                    "A5. created_at không thay đổi");

        TEST_ASSERT(cJSON_GetArraySize(answers) == 2, "A2. answers_json == NULL giữ nguyên toàn bộ 2 đáp án");
        int cur_a1_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(answers, 0), "answer_id")->valueint;
        int cur_a2_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(answers, 1), "answer_id")->valueint;
        TEST_ASSERT(cur_a1_id == orig_a1_id && cur_a2_id == orig_a2_id,
                    "A3. answer_id không thay đổi khi giữ nguyên đáp án");

        cJSON_Delete(root);
        free(det_after);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM B — THAY THẾ ĐÁP ÁN
 * ========================================================================= */
static void test_group_b_replace_answers(void)
{
    printf("\n--- Nhóm B: Thay thế danh sách đáp án ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0, q2 = 0;
    question_create(db, "Câu hỏi 1", "Network", "EASY", FIXTURE_ANSWERS_2, &q1);
    question_create(db, "Câu hỏi 2", "OS", "HARD", FIXTURE_ANSWERS_2, &q2);

    /* Lấy ID đáp án cũ của q1 và q2 */
    char *det_q1 = NULL, *det_q2 = NULL;
    question_get_detail(db, q1, &det_q1);
    question_get_detail(db, q2, &det_q2);
    cJSON *r_q1 = cJSON_Parse(det_q1);
    cJSON *r_q2 = cJSON_Parse(det_q2);
    int old_q1_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r_q1, "answers"), 0), "answer_id")->valueint;
    int old_q1_a2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r_q1, "answers"), 1), "answer_id")->valueint;
    int old_q2_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r_q2, "answers"), 0), "answer_id")->valueint;
    int old_q2_a2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r_q2, "answers"), 1), "answer_id")->valueint;
    cJSON_Delete(r_q1); cJSON_Delete(r_q2);
    free(det_q1); free(det_q2);

    /* 1. Thay thế 2 đáp án bằng 4 đáp án */
    int rc = question_update(db, q1, "Câu hỏi 1 cập nhật 4 đáp án", "Network", "HARD", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "B1a. question_update() thay thế 4 đáp án thành công");

    /* 2. Kiểm tra đáp án cũ bị xóa hoàn toàn khỏi DB */
    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE id IN (%d, %d);", old_q1_a1, old_q1_a2);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 0, "B2. Đáp án cũ của q1 đã bị xóa hoàn toàn khỏi CSDL");

    /* 3. Kiểm tra danh sách mới có đúng 4 đáp án và đúng 1 đáp án đúng */
    det_q1 = NULL;
    question_get_detail(db, q1, &det_q1);
    if (det_q1) {
        cJSON *root = cJSON_Parse(det_q1);
        cJSON *ans_arr = cJSON_GetObjectItemCaseSensitive(root, "answers");
        TEST_ASSERT(cJSON_GetArraySize(ans_arr) == 4, "B1b. Số lượng đáp án mới đúng bằng 4");

        int correct_count = 0;
        for (int i = 0; i < cJSON_GetArraySize(ans_arr); i++) {
            cJSON *item = cJSON_GetArrayItem(ans_arr, i);
            cJSON *ic = cJSON_GetObjectItemCaseSensitive(item, "is_correct");
            if (ic && ic->valueint == 1) correct_count++;
        }
        TEST_ASSERT(correct_count == 1, "B3. Danh sách mới có đúng 1 đáp án đúng");

        /* 4. Nội dung và cờ is_correct lưu chính xác */
        cJSON *item0 = cJSON_GetArrayItem(ans_arr, 0);
        cJSON *cnt0 = cJSON_GetObjectItemCaseSensitive(item0, "content");
        cJSON *ic0 = cJSON_GetObjectItemCaseSensitive(item0, "is_correct");
        TEST_ASSERT(cnt0 && strcmp(cnt0->valuestring, "Transport Layer (Tầng 4)") == 0,
                    "B4a. Nội dung đáp án 1 lưu chính xác");
        TEST_ASSERT(ic0 && ic0->valueint == 1, "B4b. Cờ is_correct đáp án 1 chính xác");

        cJSON_Delete(root);
        free(det_q1);
    }

    /* 5. Thay thế nhiều lần trên câu hỏi chưa sử dụng */
    rc = question_update(db, q1, "Câu hỏi 1 thay thế lần 2", "Network", "MEDIUM", FIXTURE_ANSWERS_2);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "B5a. Thay thế đáp án lần 2 thành công");
    rc = question_update(db, q1, "Câu hỏi 1 thay thế lần 3", "Network", "EASY", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "B5b. Thay thế đáp án lần 3 thành công");

    /* 6. Không ảnh hưởng đáp án của câu hỏi khác (q2) */
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q2);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "B6a. Số lượng đáp án của q2 vẫn nguyên vẹn 2");

    det_q2 = NULL;
    question_get_detail(db, q2, &det_q2);
    if (det_q2) {
        cJSON *root2 = cJSON_Parse(det_q2);
        cJSON *ans_arr2 = cJSON_GetObjectItemCaseSensitive(root2, "answers");
        int q2_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr2, 0), "answer_id")->valueint;
        int q2_a2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr2, 1), "answer_id")->valueint;
        TEST_ASSERT(q2_a1 == old_q2_a1 && q2_a2 == old_q2_a2,
                    "B6b. ID đáp án của câu hỏi khác (q2) không bị ảnh hưởng");
        cJSON_Delete(root2);
        free(det_q2);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM C — VALIDATION UPDATE
 * ========================================================================= */
static void test_group_c_validation_update(void)
{
    printf("\n--- Nhóm C: Validation của question_update() ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0;
    question_create(db, "Câu hỏi gốc", "Topic gốc", "EASY", FIXTURE_ANSWERS_2, &q1);

    /* Tham số con trỏ và ID cơ bản */
    TEST_ASSERT(question_update(NULL, q1, "C", "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C1. db == NULL -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, 0, "C", "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C2a. question_id = 0 -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, -1, "C", "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C2b. question_id < 0 -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, NULL, "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C3a. content == NULL -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", NULL, "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C3b. topic == NULL -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", "T", NULL, NULL) == QUESTION_ERR_INVALID_PARAM,
                "C3c. difficulty == NULL -> QUESTION_ERR_INVALID_PARAM");

    /* Whitespace / rỗng */
    TEST_ASSERT(question_update(db, q1, "", "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C4a. content rỗng -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "   \t\n  ", "T", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C4b. content whitespace -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", "", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C4c. topic rỗng -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", "   ", "EASY", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C4d. topic whitespace -> QUESTION_ERR_INVALID_PARAM");

    /* Difficulty không hợp lệ */
    TEST_ASSERT(question_update(db, q1, "C", "T", "VERY_HARD", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C5a. difficulty 'VERY_HARD' -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", "T", "easy", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C5b. difficulty chữ thường 'easy' -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_update(db, q1, "C", "T", "", NULL) == QUESTION_ERR_INVALID_PARAM,
                "C5c. difficulty rỗng -> QUESTION_ERR_INVALID_PARAM");

    /* answers_json sai cú pháp / cấu trúc */
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", "invalid json{") == QUESTION_ERR_INVALID_DATA,
                "C6. answers_json sai cú pháp -> QUESTION_ERR_INVALID_DATA");
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", "{\"content\": \"A\", \"is_correct\": 1}") == QUESTION_ERR_INVALID_DATA,
                "C7. JSON là Object thay vì Array -> QUESTION_ERR_INVALID_DATA");
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", "[]") == QUESTION_ERR_INVALID_DATA,
                "C8a. Array rỗng -> QUESTION_ERR_INVALID_DATA");
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", "[{\"content\": \"A\", \"is_correct\": 1}]") == QUESTION_ERR_INVALID_DATA,
                "C8b. Chỉ có 1 đáp án -> QUESTION_ERR_INVALID_DATA");

    /* Số lượng đáp án đúng sai */
    const char *no_correct = "[{\"content\":\"A\",\"is_correct\":0},{\"content\":\"B\",\"is_correct\":0}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", no_correct) == QUESTION_ERR_INVALID_DATA,
                "C9a. 0 đáp án đúng -> QUESTION_ERR_INVALID_DATA");
    const char *two_correct = "[{\"content\":\"A\",\"is_correct\":1},{\"content\":\"B\",\"is_correct\":1}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", two_correct) == QUESTION_ERR_INVALID_DATA,
                "C9b. 2 đáp án đúng -> QUESTION_ERR_INVALID_DATA");

    /* is_correct sai kiểu / out-of-range */
    const char *bool_correct = "[{\"content\":\"A\",\"is_correct\":true},{\"content\":\"B\",\"is_correct\":false}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", bool_correct) == QUESTION_ERR_INVALID_DATA,
                "C10a. is_correct là boolean -> QUESTION_ERR_INVALID_DATA");
    const char *float_correct = "[{\"content\":\"A\",\"is_correct\":1.5},{\"content\":\"B\",\"is_correct\":0}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", float_correct) == QUESTION_ERR_INVALID_DATA,
                "C10b. is_correct là float -> QUESTION_ERR_INVALID_DATA");
    const char *range_correct = "[{\"content\":\"A\",\"is_correct\":2},{\"content\":\"B\",\"is_correct\":0}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", range_correct) == QUESTION_ERR_INVALID_DATA,
                "C10c. is_correct = 2 -> QUESTION_ERR_INVALID_DATA");

    /* Thiếu khóa hoặc khóa trùng */
    const char *missing_key = "[{\"content\":\"A\"},{\"content\":\"B\",\"is_correct\":0}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", missing_key) == QUESTION_ERR_INVALID_DATA,
                "C11a. Thiếu khóa is_correct -> QUESTION_ERR_INVALID_DATA");
    const char *dup_key = "[{\"content\":\"A\",\"content\":\"B\",\"is_correct\":1},{\"content\":\"C\",\"is_correct\":0}]";
    TEST_ASSERT(question_update(db, q1, "C", "T", "EASY", dup_key) == QUESTION_ERR_INVALID_DATA,
                "C11b. Khóa content bị trùng -> QUESTION_ERR_INVALID_DATA");

    /* ID không tồn tại */
    TEST_ASSERT(question_update(db, 99999, "C", "T", "EASY", NULL) == QUESTION_ERR_NOT_FOUND,
                "C12. ID không tồn tại -> QUESTION_ERR_NOT_FOUND");

    /* Xác minh dữ liệu cũ trong CSDL hoàn toàn không bị thay đổi */
    char *det = NULL;
    question_get_detail(db, q1, &det);
    if (det) {
        cJSON *root = cJSON_Parse(det);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *top = cJSON_GetObjectItemCaseSensitive(root, "topic");
        cJSON *ans = cJSON_GetObjectItemCaseSensitive(root, "answers");
        TEST_ASSERT(cnt && strcmp(cnt->valuestring, "Câu hỏi gốc") == 0,
                    "C13a. Dữ liệu content cũ giữ nguyên sau loạt validation thất bại");
        TEST_ASSERT(top && strcmp(top->valuestring, "Topic gốc") == 0,
                    "C13b. Dữ liệu topic cũ giữ nguyên sau loạt validation thất bại");
        TEST_ASSERT(cJSON_GetArraySize(ans) == 2,
                    "C13c. Số lượng đáp án giữ nguyên sau validation thất bại");
        cJSON_Delete(root);
        free(det);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM D — BẢO VỆ IN_USE CHO UPDATE
 * ========================================================================= */
static void test_group_d_in_use_update(void)
{
    printf("\n--- Nhóm D: Bảo vệ IN_USE cho Update ---\n");
    sqlite3 *db = setup_test_db();

    /* Nạp bản ghi FK hợp lệ */
    exec_sql(db, "INSERT INTO users(username, password_hash) VALUES('alice', 'hash');");
    exec_sql(db, "INSERT INTO rooms(owner_id, name, question_count, duration_minutes) VALUES(1, 'Room 1', 5, 30);");
    exec_sql(db, "INSERT INTO exam_attempts(user_id, mode) VALUES(1, 'PRACTICE');");

    int q_prac = 0, q_room = 0, q_att = 0;
    question_create(db, "Q Practice", "Network", "EASY", FIXTURE_ANSWERS_2, &q_prac);
    question_create(db, "Q Room", "OS", "MEDIUM", FIXTURE_ANSWERS_2, &q_room);
    question_create(db, "Q Attempt", "Database", "HARD", FIXTURE_ANSWERS_2, &q_att);

    /* Tham chiếu bảng practice_questions */
    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO practice_questions(attempt_id, question_id, position) VALUES(1, %d, 1);", q_prac);
    exec_sql(db, sql_buf);

    /* 1. practice_questions: answers_json == NULL bị từ chối */
    int rc = question_update(db, q_prac, "New content", "Network", "HARD", NULL);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D1a. Update content (answers_json=NULL) khi trong practice_questions trả IN_USE");
    /* 2. practice_questions: answers_json != NULL bị từ chối */
    rc = question_update(db, q_prac, "New content", "Network", "HARD", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D1b. Update replace answers khi trong practice_questions trả IN_USE");

    /* Tham chiếu bảng room_questions */
    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO room_questions(room_id, question_id, position) VALUES(1, %d, 1);", q_room);
    exec_sql(db, sql_buf);

    /* 3. room_questions: bị từ chối cả 2 trường hợp */
    rc = question_update(db, q_room, "New content", "OS", "EASY", NULL);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D2a. Update content khi trong room_questions trả IN_USE");
    rc = question_update(db, q_room, "New content", "OS", "EASY", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D2b. Update replace answers khi trong room_questions trả IN_USE");

    /* Tham chiếu bảng attempt_answers */
    char *det = NULL;
    question_get_detail(db, q_att, &det);
    cJSON *r = cJSON_Parse(det);
    int ans_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "answers"), 0), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO attempt_answers(attempt_id, question_id, answer_id) VALUES(1, %d, %d);", q_att, ans_id);
    exec_sql(db, sql_buf);

    /* 4. attempt_answers: bị từ chối cả 2 trường hợp */
    rc = question_update(db, q_att, "New content", "Database", "EASY", NULL);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D3a. Update content khi trong attempt_answers trả IN_USE");
    rc = question_update(db, q_att, "New content", "Database", "EASY", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "D3b. Update replace answers khi trong attempt_answers trả IN_USE");

    /* 5. Xác minh câu hỏi và đáp án không thay đổi */
    det = NULL;
    question_get_detail(db, q_prac, &det);
    if (det) {
        cJSON *root = cJSON_Parse(det);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "content");
        TEST_ASSERT(cnt && strcmp(cnt->valuestring, "Q Practice") == 0,
                    "D4. Câu hỏi và đáp án được bảo vệ nguyên vẹn sau IN_USE");
        cJSON_Delete(root);
        free(det);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM E — TRANSACTION UPDATE
 * ========================================================================= */
static void test_group_e_transaction_update(void)
{
    printf("\n--- Nhóm E: Transaction và Rollback của question_update() ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0;
    question_create(db, "Nội dung ban đầu", "Topic A", "EASY", FIXTURE_ANSWERS_2, &q1);

    /* Lấy danh sách ID đáp án ban đầu */
    char *det = NULL;
    question_get_detail(db, q1, &det);
    cJSON *r = cJSON_Parse(det);
    int old_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "answers"), 0), "answer_id")->valueint;
    int old_a2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "answers"), 1), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    /* Cài trigger gây lỗi khi chèn đáp án mới */
    exec_sql(db,
        "CREATE TRIGGER trigger_fail_insert_answers BEFORE INSERT ON answers "
        "BEGIN SELECT RAISE(ABORT, 'force answers insert failure'); END;");

    int rc = question_update(db, q1, "Nội dung đã sửa tạm", "Topic B", "HARD", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "E1. Trả về QUESTION_ERR_DATABASE khi trigger chặn");

    /* Kiểm tra CSDL sau rollback */
    det = NULL;
    rc = question_get_detail(db, q1, &det);
    TEST_ASSERT(rc == QUESTION_SUCCESS && det != NULL, "E2a. Đọc lại câu hỏi thành công");
    if (det) {
        cJSON *root = cJSON_Parse(det);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *top = cJSON_GetObjectItemCaseSensitive(root, "topic");
        cJSON *dif = cJSON_GetObjectItemCaseSensitive(root, "difficulty");
        cJSON *ans = cJSON_GetObjectItemCaseSensitive(root, "answers");

        TEST_ASSERT(cnt && strcmp(cnt->valuestring, "Nội dung ban đầu") == 0,
                    "E2b. Nội dung câu hỏi được khôi phục về giá trị cũ");
        TEST_ASSERT(top && strcmp(top->valuestring, "Topic A") == 0,
                    "E2c. Topic câu hỏi được khôi phục về giá trị cũ");
        TEST_ASSERT(dif && strcmp(dif->valuestring, "EASY") == 0,
                    "E2d. Difficulty câu hỏi được khôi phục về giá trị cũ");
        TEST_ASSERT(cJSON_GetArraySize(ans) == 2,
                    "E3a. Đáp án cũ được khôi phục nguyên vẹn 2 phần tử");

        int cur_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans, 0), "answer_id")->valueint;
        int cur_a2 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans, 1), "answer_id")->valueint;
        TEST_ASSERT(cur_a1 == old_a1 && cur_a2 == old_a2,
                    "E3b. Không có đáp án mới nào bị lưu dở dang (ID đáp án cũ bảo toàn)");

        cJSON_Delete(root);
        free(det);
    }

    /* Gỡ trigger và thực hiện cập nhật hợp lệ */
    exec_sql(db, "DROP TRIGGER trigger_fail_insert_answers;");
    rc = question_update(db, q1, "Nội dung cập nhật thành công", "Topic Mới", "HARD", FIXTURE_ANSWERS_4);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "E4. Sau khi gỡ trigger, cập nhật hợp lệ thành công");

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM F — DELETE CƠ BẢN
 * ========================================================================= */
static void test_group_f_delete_basic(void)
{
    printf("\n--- Nhóm F: Delete cơ bản ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0, q2 = 0;
    question_create(db, "Q1 cần xóa", "Topic", "EASY", FIXTURE_ANSWERS_2, &q1);
    question_create(db, "Q2 giữ lại", "Topic", "HARD", FIXTURE_ANSWERS_2, &q2);

    /* 1. Xóa câu hỏi chưa sử dụng thành công */
    int rc = question_delete(db, q1);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "F1. question_delete(q1) trả về QUESTION_SUCCESS");

    /* 2. Xóa toàn bộ đáp án thuộc câu hỏi */
    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 0, "F2a. Câu hỏi q1 không còn trong bảng questions");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 0, "F2b. Toàn bộ đáp án của q1 đã bị xóa khỏi bảng answers");

    /* 3. Không ảnh hưởng câu hỏi khác (q2) */
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q2);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "F3a. Câu hỏi q2 vẫn tồn tại");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q2);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "F3b. Các đáp án của q2 vẫn tồn tại đủ 2");

    /* 4. Xóa lần hai trả QUESTION_ERR_NOT_FOUND */
    rc = question_delete(db, q1);
    TEST_ASSERT(rc == QUESTION_ERR_NOT_FOUND, "F4. Xóa lại q1 lần thứ hai trả về QUESTION_ERR_NOT_FOUND");

    /* 5. ID không tồn tại trả QUESTION_ERR_NOT_FOUND */
    rc = question_delete(db, 88888);
    TEST_ASSERT(rc == QUESTION_ERR_NOT_FOUND, "F5. ID không tồn tại (88888) trả về QUESTION_ERR_NOT_FOUND");

    /* 6. db == NULL hoặc ID <= 0 trả QUESTION_ERR_INVALID_PARAM */
    TEST_ASSERT(question_delete(NULL, q2) == QUESTION_ERR_INVALID_PARAM, "F6a. db == NULL -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_delete(db, 0) == QUESTION_ERR_INVALID_PARAM, "F6b. question_id = 0 -> QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(question_delete(db, -5) == QUESTION_ERR_INVALID_PARAM, "F6c. question_id < 0 -> QUESTION_ERR_INVALID_PARAM");

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM G — BẢO VỆ IN_USE CHO DELETE
 * ========================================================================= */
static void test_group_g_in_use_delete(void)
{
    printf("\n--- Nhóm G: Bảo vệ IN_USE cho Delete ---\n");
    sqlite3 *db = setup_test_db();

    exec_sql(db, "INSERT INTO users(username, password_hash) VALUES('carol', 'hash');");
    exec_sql(db, "INSERT INTO rooms(owner_id, name, question_count, duration_minutes) VALUES(1, 'Room 1', 5, 30);");
    exec_sql(db, "INSERT INTO exam_attempts(user_id, mode) VALUES(1, 'PRACTICE');");

    int q_prac = 0, q_room = 0, q_att = 0;
    question_create(db, "Q Practice Del", "Network", "EASY", FIXTURE_ANSWERS_2, &q_prac);
    question_create(db, "Q Room Del", "OS", "MEDIUM", FIXTURE_ANSWERS_2, &q_room);
    question_create(db, "Q Attempt Del", "Database", "HARD", FIXTURE_ANSWERS_2, &q_att);

    /* 1. Tham chiếu practice_questions */
    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO practice_questions(attempt_id, question_id, position) VALUES(1, %d, 1);", q_prac);
    exec_sql(db, sql_buf);

    int rc = question_delete(db, q_prac);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "G1a. Xóa câu hỏi trong practice_questions trả về QUESTION_ERR_IN_USE");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q_prac);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "G1b. Câu hỏi q_prac vẫn tồn tại");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q_prac);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "G1c. Toàn bộ đáp án q_prac vẫn tồn tại");

    /* 2. Tham chiếu room_questions */
    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO room_questions(room_id, question_id, position) VALUES(1, %d, 1);", q_room);
    exec_sql(db, sql_buf);

    rc = question_delete(db, q_room);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "G2a. Xóa câu hỏi trong room_questions trả về QUESTION_ERR_IN_USE");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q_room);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "G2b. Câu hỏi q_room vẫn tồn tại");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q_room);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "G2c. Toàn bộ đáp án q_room vẫn tồn tại");

    /* 3. Tham chiếu attempt_answers */
    char *det = NULL;
    question_get_detail(db, q_att, &det);
    cJSON *r = cJSON_Parse(det);
    int ans_id = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(r, "answers"), 0), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    snprintf(sql_buf, sizeof(sql_buf), "INSERT INTO attempt_answers(attempt_id, question_id, answer_id) VALUES(1, %d, %d);", q_att, ans_id);
    exec_sql(db, sql_buf);

    rc = question_delete(db, q_att);
    TEST_ASSERT(rc == QUESTION_ERR_IN_USE, "G3a. Xóa câu hỏi trong attempt_answers trả về QUESTION_ERR_IN_USE");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q_att);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "G3b. Câu hỏi q_att vẫn tồn tại");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q_att);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "G3c. Toàn bộ đáp án q_att vẫn tồn tại");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM attempt_answers WHERE question_id = %d;", q_att);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "G3d. Dữ liệu tham chiếu attempt_answers vẫn tồn tại");

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM H — TRANSACTION DELETE
 * ========================================================================= */
static void test_group_h_transaction_delete(void)
{
    printf("\n--- Nhóm H: Transaction và Rollback của question_delete() ---\n");
    sqlite3 *db = setup_test_db();

    int q1 = 0;
    question_create(db, "Câu hỏi test rollback delete", "Network", "EASY", FIXTURE_ANSWERS_2, &q1);

    /* Cài trigger chặn DELETE trên bảng questions */
    exec_sql(db,
        "CREATE TRIGGER reject_question_delete BEFORE DELETE ON questions "
        "BEGIN SELECT RAISE(ABORT, 'forced delete failure'); END;");

    int rc = question_delete(db, q1);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "H1. question_delete trả về QUESTION_ERR_DATABASE khi trigger chặn");

    /* Kiểm tra CSDL: câu hỏi và đáp án đã được rollback phục hồi */
    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "H2a. Câu hỏi q1 được khôi phục nguyên vẹn sau rollback");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "H2b. Toàn bộ 2 đáp án được khôi phục nguyên vẹn sau rollback");

    /* Gỡ trigger và xóa thành công */
    exec_sql(db, "DROP TRIGGER reject_question_delete;");
    rc = question_delete(db, q1);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "H3. Sau khi gỡ trigger, xóa câu hỏi thành công");
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 0, "H4. Câu hỏi đã bị xóa hoàn toàn");

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM I — LỖI SQLITE VÀ TRẠNG THÁI TRANSACTION
 * ========================================================================= */
static void test_group_i_sqlite_errors(void)
{
    printf("\n--- Nhóm I: Lỗi SQLite và trạng thái Autocommit ---\n");
    sqlite3 *empty_db = NULL;
    int rc = sqlite3_open(":memory:", &empty_db);
    TEST_ASSERT(rc == SQLITE_OK && empty_db != NULL, "I1. Mở database rỗng thành công");

    /* Gọi question_update trên database thiếu bảng */
    rc = question_update(empty_db, 1, "C", "T", "EASY", NULL);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "I2a. question_update() thiếu bảng trả về QUESTION_ERR_DATABASE");
    TEST_ASSERT(sqlite3_get_autocommit(empty_db) != 0, "I2b. Autocommit bật (không bị kẹt transaction dở dang)");

    /* Gọi question_delete trên database thiếu bảng */
    rc = question_delete(empty_db, 1);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "I3a. question_delete() thiếu bảng trả về QUESTION_ERR_DATABASE");
    TEST_ASSERT(sqlite3_get_autocommit(empty_db) != 0, "I3b. Autocommit bật (không bị kẹt transaction dở dang)");

    sqlite3_close(empty_db);
}

int main(void)
{
    printf("================================================================\n");
    printf("UNIT TESTS: QUESTION_UPDATE() & QUESTION_DELETE() (TASK 05.5.3)\n");
    printf("================================================================\n");

    test_group_a_update_info();
    test_group_b_replace_answers();
    test_group_c_validation_update();
    test_group_d_in_use_update();
    test_group_e_transaction_update();
    test_group_f_delete_basic();
    test_group_g_in_use_delete();
    test_group_h_transaction_delete();
    test_group_i_sqlite_errors();

    printf("\n================================================================\n");
    printf("QUESTION UPDATE/DELETE TEST RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}

