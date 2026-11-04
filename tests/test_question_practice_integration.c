#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "question.h"
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

    /* Tạo người dùng mẫu trong users */
    exec_sql(db, "INSERT INTO users(username, password_hash, role) VALUES('student1', 'pass_hash', 'USER');");

    return db;
}

int main(void)
{
    printf("================================================================\n");
    printf("INTEGRATION TESTS: QUESTION MANAGEMENT x PRACTICE (TASK 05.6)\n");
    printf("================================================================\n\n");

    sqlite3 *db = setup_test_db();
    if (!db) return 1;

    /* =========================================================================
     * KỊCH BẢN A — TẠO QUESTION RỒI BẮT ĐẦU PRACTICE
     * ========================================================================= */
    printf("--- Kịch bản A: Tạo Question rồi bắt đầu Practice ---\n");

    const char *ans_q1_json =
        "["
        "  {\"content\": \"Transport Layer (Tầng giao vận)\", \"is_correct\": 1},"
        "  {\"content\": \"Network Layer (Tầng mạng)\", \"is_correct\": 0}"
        "]";
    const char *ans_q2_json =
        "["
        "  {\"content\": \"Không (Connectionless)\", \"is_correct\": 1},"
        "  {\"content\": \"Có (Connection-oriented)\", \"is_correct\": 0}"
        "]";
    const char *ans_q3_json =
        "["
        "  {\"content\": \"Cổng 80\", \"is_correct\": 1},"
        "  {\"content\": \"Cổng 443\", \"is_correct\": 0}"
        "]";

    int q1 = 0, q2 = 0, q3 = 0;
    int rc1 = question_create(db, "TCP hoạt động ở tầng nào trong OSI?", "Network", "EASY", ans_q1_json, &q1);
    int rc2 = question_create(db, "UDP có cơ chế bắt tay 3 bước không?", "Network", "EASY", ans_q2_json, &q2);
    int rc3 = question_create(db, "Giao thức HTTP mặc định sử dụng cổng TCP nào?", "Network", "EASY", ans_q3_json, &q3);

    TEST_ASSERT(rc1 == QUESTION_SUCCESS && q1 > 0, "A1a. Tạo câu hỏi q1 thành công qua question_create()");
    TEST_ASSERT(rc2 == QUESTION_SUCCESS && q2 > 0, "A1b. Tạo câu hỏi q2 thành công qua question_create()");
    TEST_ASSERT(rc3 == QUESTION_SUCCESS && q3 > 0, "A1c. Tạo câu hỏi q3 thành công qua question_create()");

    /* Lấy answer_id của từng đáp án để dùng cho submit sau này */
    int q1_correct_ans = 0, q1_wrong_ans = 0;
    int q2_correct_ans = 0, q2_wrong_ans = 0;
    int q3_correct_ans = 0, q3_wrong_ans = 0;

    char *det = NULL;
    question_get_detail(db, q1, &det);
    cJSON *r = cJSON_Parse(det);
    cJSON *ans_arr = cJSON_GetObjectItemCaseSensitive(r, "answers");
    q1_correct_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 0), "answer_id")->valueint;
    q1_wrong_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 1), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    question_get_detail(db, q2, &det);
    r = cJSON_Parse(det);
    ans_arr = cJSON_GetObjectItemCaseSensitive(r, "answers");
    q2_correct_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 0), "answer_id")->valueint;
    q2_wrong_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 1), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    question_get_detail(db, q3, &det);
    r = cJSON_Parse(det);
    ans_arr = cJSON_GetObjectItemCaseSensitive(r, "answers");
    q3_correct_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 0), "answer_id")->valueint;
    q3_wrong_ans = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(ans_arr, 1), "answer_id")->valueint;
    cJSON_Delete(r); free(det);

    (void)q1_wrong_ans;
    (void)q2_wrong_ans;
    (void)q3_correct_ans;

    /* Bắt đầu bài luyện tập 3 câu hỏi với topic 'Network' và difficulty 'EASY' */
    char *practice_json = NULL;
    int attempt_id = practice_start(db, 1, "Network", "EASY", 3, &practice_json);
    TEST_ASSERT(attempt_id > 0, "A2. practice_start() khởi tạo attempt thành công (attempt_id > 0)");
    TEST_ASSERT(practice_json != NULL, "A3a. JSON câu hỏi practice không NULL");

    if (practice_json) {
        cJSON *root = cJSON_Parse(practice_json);
        TEST_ASSERT(root != NULL, "A3b. Parse JSON practice thành công");
        cJSON *total_q = cJSON_GetObjectItemCaseSensitive(root, "total_questions");
        cJSON *q_list = cJSON_GetObjectItemCaseSensitive(root, "questions");

        TEST_ASSERT(total_q && total_q->valueint == 3, "A3c. total_questions trong JSON bằng 3");
        TEST_ASSERT(q_list && cJSON_GetArraySize(q_list) == 3, "A3d. Mảng questions chứa đúng 3 phần tử");

        /* Kiểm tra không rò rỉ đáp án đúng trong JSON */
        int leak_detected = 0;
        for (int i = 0; i < cJSON_GetArraySize(q_list); i++) {
            cJSON *item = cJSON_GetArrayItem(q_list, i);
            if (cJSON_GetObjectItemCaseSensitive(item, "is_correct") != NULL ||
                cJSON_GetObjectItemCaseSensitive(item, "correct_answer_id") != NULL) {
                leak_detected = 1;
            }
            cJSON *ans = cJSON_GetObjectItemCaseSensitive(item, "answers");
            if (ans) {
                for (int j = 0; j < cJSON_GetArraySize(ans); j++) {
                    cJSON *a_item = cJSON_GetArrayItem(ans, j);
                    if (cJSON_GetObjectItemCaseSensitive(a_item, "is_correct") != NULL) {
                        leak_detected = 1;
                    }
                }
            }
        }
        TEST_ASSERT(leak_detected == 0, "A4. Không có trường is_correct hoặc correct_answer_id trong JSON gửi client");

        cJSON_Delete(root);
        free(practice_json);
    }

    char sql_buf[256];
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM practice_questions WHERE attempt_id = %d;", attempt_id);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 3, "A5. Bảng practice_questions ghi nhận đủ 3 câu hỏi cho attempt");

    /* =========================================================================
     * KỊCH BẢN B — CHẶN SỬA CÂU HỎI ĐÃ ĐƯỢC CHỌN VÀO PRACTICE
     * ========================================================================= */
    printf("\n--- Kịch bản B: Chặn sửa câu hỏi đã được chọn ---\n");

    /* 1. Gọi question_update() với answers_json == NULL */
    int rc_up1 = question_update(db, q1, "TCP sửa đổi content", "Network", "EASY", NULL);
    TEST_ASSERT(rc_up1 == QUESTION_ERR_IN_USE, "B1. question_update(answers_json=NULL) trả về QUESTION_ERR_IN_USE");

    /* 2. Gọi question_update() với answers_json != NULL */
    const char *new_answers =
        "["
        "  {\"content\": \"Transport Mới\", \"is_correct\": 1},"
        "  {\"content\": \"Network Mới\", \"is_correct\": 0}"
        "]";
    int rc_up2 = question_update(db, q1, "TCP thay thế đáp án", "Network", "HARD", new_answers);
    TEST_ASSERT(rc_up2 == QUESTION_ERR_IN_USE, "B2. question_update(answers_json!=NULL) trả về QUESTION_ERR_IN_USE");

    /* 3. Xác minh nội dung và đáp án của q1 không đổi */
    det = NULL;
    question_get_detail(db, q1, &det);
    if (det) {
        cJSON *root = cJSON_Parse(det);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "content");
        cJSON *answers = cJSON_GetObjectItemCaseSensitive(root, "answers");
        TEST_ASSERT(cnt && strcmp(cnt->valuestring, "TCP hoạt động ở tầng nào trong OSI?") == 0,
                    "B3a. Nội dung câu hỏi q1 không bị thay đổi");
        int cur_a1 = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(answers, 0), "answer_id")->valueint;
        TEST_ASSERT(cur_a1 == q1_correct_ans, "B3b. Đáp án cũ của q1 vẫn được giữ nguyên");
        cJSON_Delete(root);
        free(det);
    }

    /* =========================================================================
     * KỊCH BẢN C — CHẶN XÓA CÂU HỎI ĐÃ ĐƯỢC CHỌN
     * ========================================================================= */
    printf("\n--- Kịch bản C: Chặn xóa câu hỏi đã được chọn ---\n");

    int rc_del = question_delete(db, q1);
    TEST_ASSERT(rc_del == QUESTION_ERR_IN_USE, "C1. question_delete(q1) trả về QUESTION_ERR_IN_USE");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "C2a. Bản ghi q1 vẫn tồn tại trong bảng questions");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM answers WHERE question_id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 2, "C2b. Toàn bộ đáp án của q1 vẫn tồn tại trong bảng answers");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM practice_questions WHERE question_id = %d;", q1);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "C3. Bản ghi trong practice_questions vẫn tồn tại nguyên vẹn");

    /* =========================================================================
     * KỊCH BẢN D — NỘP BÀI VÀ CHẤM ĐIỂM
     * ========================================================================= */
    printf("\n--- Kịch bản D: Nộp bài và chấm điểm ---\n");

    /* Nộp 3 câu: q1 đúng (q1_correct_ans), q2 đúng (q2_correct_ans), q3 sai (q3_wrong_ans)
     * Điểm số dự kiến: 10.0 * 2 / 3 = 6.666667 (làm tròn hoặc double 6.666667)
     */
    char submit_payload[512];
    snprintf(submit_payload, sizeof(submit_payload),
             "["
             "  {\"question_id\": %d, \"answer_id\": %d},"
             "  {\"question_id\": %d, \"answer_id\": %d},"
             "  {\"question_id\": %d, \"answer_id\": %d}"
             "]",
             q1, q1_correct_ans,
             q2, q2_correct_ans,
             q3, q3_wrong_ans);

    char *result_json = NULL;
    int rc_submit = practice_submit(db, attempt_id, 1, submit_payload, &result_json);
    TEST_ASSERT(rc_submit == PRACTICE_SUCCESS, "D1. practice_submit() thực hiện thành công (PRACTICE_SUCCESS)");
    TEST_ASSERT(result_json != NULL, "D2a. result_json không NULL sau khi nộp");

    double expected_score = (2.0 / 3.0) * 10.0;
    if (result_json) {
        cJSON *root = cJSON_Parse(result_json);
        cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "score");
        cJSON *cor = cJSON_GetObjectItemCaseSensitive(root, "correct_count");
        cJSON *tot = cJSON_GetObjectItemCaseSensitive(root, "total_questions");

        TEST_ASSERT(cor && cor->valueint == 2, "D2b. correct_count trong JSON bằng đúng 2");
        TEST_ASSERT(tot && tot->valueint == 3, "D2c. total_questions trong JSON bằng đúng 3");
        TEST_ASSERT(sc && fabs(sc->valuedouble - expected_score) < 0.01,
                    "D2d. Điểm số trong JSON đúng công thức 10.0 * 2 / 3 (~6.67)");

        cJSON_Delete(root);
        free(result_json);
    }

    /* Kiểm tra CSDL bảng attempt_answers */
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM attempt_answers WHERE attempt_id = %d;", attempt_id);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 3, "D3. Bảng attempt_answers lưu đủ 3 câu trả lời đã nộp");

    /* Kiểm tra CSDL bảng exam_attempts: submitted_at và score */
    snprintf(sql_buf, sizeof(sql_buf),
             "SELECT COUNT(*) FROM exam_attempts WHERE id = %d AND submitted_at IS NOT NULL AND ABS(score - %f) < 0.01;",
             attempt_id, expected_score);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1,
                "D4. exam_attempts đã cập nhật submitted_at và điểm số khớp với JSON");

    /* =========================================================================
     * KỊCH BẢN E — BẢO VỆ DỮ LIỆU SAU KHI NỘP BÀI
     * ========================================================================= */
    printf("\n--- Kịch bản E: Bảo vệ dữ liệu sau khi nộp bài ---\n");

    /* Sau submit, câu hỏi đã nằm trong cả practice_questions VÀ attempt_answers */
    rc_up1 = question_update(db, q1, "TCP sửa sau nộp", "Network", "HARD", NULL);
    TEST_ASSERT(rc_up1 == QUESTION_ERR_IN_USE, "E1. question_update() sau khi nộp bài vẫn trả QUESTION_ERR_IN_USE");

    rc_del = question_delete(db, q1);
    TEST_ASSERT(rc_del == QUESTION_ERR_IN_USE, "E2a. question_delete(q1) sau khi nộp bài vẫn trả QUESTION_ERR_IN_USE");
    TEST_ASSERT(question_delete(db, q2) == QUESTION_ERR_IN_USE, "E2b. question_delete(q2) trả QUESTION_ERR_IN_USE");
    TEST_ASSERT(question_delete(db, q3) == QUESTION_ERR_IN_USE, "E2c. question_delete(q3) trả QUESTION_ERR_IN_USE");

    /* Xác minh dữ liệu câu hỏi, đáp án và lịch sử nộp bài vẫn nguyên vẹn */
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id IN (%d, %d, %d);", q1, q2, q3);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 3, "E3a. Cả 3 câu hỏi vẫn tồn tại đầy đủ trong questions");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM attempt_answers WHERE attempt_id = %d;", attempt_id);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 3, "E3b. Lịch sử bài nộp trong attempt_answers vẫn nguyên vẹn");

    /* =========================================================================
     * KỊCH BẢN F — CÂU HỎI CHƯA ĐƯỢC SỬ DỤNG
     * ========================================================================= */
    printf("\n--- Kịch bản F: Câu hỏi chưa được sử dụng ---\n");

    int q_independent = 0;
    int rc_q4 = question_create(db, "Câu hỏi độc lập q4?", "OS", "HARD", ans_q1_json, &q_independent);
    TEST_ASSERT(rc_q4 == QUESTION_SUCCESS && q_independent > 0, "F1. Tạo câu hỏi độc lập q4 thành công");

    /* Cập nhật q4 thành công */
    int rc_up_q4 = question_update(db, q_independent, "Câu hỏi độc lập q4 đã sửa?", "OS", "MEDIUM", NULL);
    TEST_ASSERT(rc_up_q4 == QUESTION_SUCCESS, "F2. question_update(q4) thành công vì q4 chưa được sử dụng");

    /* Xóa q4 thành công */
    int rc_del_q4 = question_delete(db, q_independent);
    TEST_ASSERT(rc_del_q4 == QUESTION_SUCCESS, "F3. question_delete(q4) thành công vì q4 chưa được sử dụng");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id = %d;", q_independent);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 0, "F4a. q4 đã bị xóa khỏi bảng questions");

    /* Xác minh attempt và 3 câu hỏi ban đầu hoàn toàn không bị ảnh hưởng */
    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM questions WHERE id IN (%d, %d, %d);", q1, q2, q3);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 3, "F4b. 3 câu hỏi trong bài practice không bị ảnh hưởng");

    snprintf(sql_buf, sizeof(sql_buf), "SELECT COUNT(*) FROM exam_attempts WHERE id = %d;", attempt_id);
    TEST_ASSERT(count_table_rows(db, sql_buf) == 1, "F4c. Lượt thi practice vẫn nguyên vẹn");

    /* =========================================================================
     * KỊCH BẢN G — TÍNH NHẤT QUÁN DỮ LIỆU VÀ TOÀN VẸN NGOẠI KHÓA
     * ========================================================================= */
    printf("\n--- Kịch bản G: Tính nhất quán dữ liệu ---\n");

    /* 1. Không có practice_questions mồ côi */
    int orphan_pq = count_table_rows(db, "SELECT COUNT(*) FROM practice_questions WHERE question_id NOT IN (SELECT id FROM questions);");
    TEST_ASSERT(orphan_pq == 0, "G1. Không có practice_questions tham chiếu câu hỏi không tồn tại");

    /* 2. Không có attempt_answers mồ côi */
    int orphan_aa_ans = count_table_rows(db, "SELECT COUNT(*) FROM attempt_answers WHERE answer_id NOT IN (SELECT id FROM answers);");
    TEST_ASSERT(orphan_aa_ans == 0, "G2a. Không có attempt_answers tham chiếu đáp án không tồn tại");

    int orphan_aa_q = count_table_rows(db, "SELECT COUNT(*) FROM attempt_answers WHERE question_id NOT IN (SELECT id FROM questions);");
    TEST_ASSERT(orphan_aa_q == 0, "G2b. Không có attempt_answers tham chiếu câu hỏi không tồn tại");

    /* 3. Kiểm tra PRAGMA foreign_key_check */
    sqlite3_stmt *stmt_fk = NULL;
    int rc_fk = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt_fk, NULL);
    TEST_ASSERT(rc_fk == SQLITE_OK && stmt_fk != NULL, "G3a. Chuẩn bị PRAGMA foreign_key_check thành công");
    int fk_violations = 0;
    while (sqlite3_step(stmt_fk) == SQLITE_ROW) {
        fk_violations++;
    }
    sqlite3_finalize(stmt_fk);
    TEST_ASSERT(fk_violations == 0, "G3b. PRAGMA foreign_key_check trả về 0 vi phạm (CSDL nhất quán 100%)");

    /* 4. Không có transaction dở dang */
    TEST_ASSERT(sqlite3_get_autocommit(db) != 0, "G4. Autocommit mode đang bật (không có transaction bị kẹt dở dang)");

    sqlite3_close(db);

    printf("\n================================================================\n");
    printf("INTEGRATION RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
