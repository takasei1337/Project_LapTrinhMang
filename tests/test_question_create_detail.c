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

/* =========================================================================
 * NHÓM A — QUESTION_CREATE THÀNH CÔNG
 * ========================================================================= */
static void test_group_a_create_success(sqlite3 *db)
{
    printf("\n--- Nhóm A: question_create() thành công ---\n");

    /* 1. Tạo câu hỏi với 2 đáp án (1 đúng, 1 sai) */
    int q_id_1 = 0;
    const char *answers_2 =
        "["
        "  {\"content\": \"Transport Layer\", \"is_correct\": 1},"
        "  {\"content\": \"Network Layer\", \"is_correct\": 0}"
        "]";
    int rc = question_create(db, "TCP hoạt động ở tầng nào trong OSI?", "Network", "MEDIUM", answers_2, &q_id_1);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "A1a. Tạo câu hỏi 2 đáp án trả về QUESTION_SUCCESS");
    TEST_ASSERT(q_id_1 > 0, "A1b. out_question_id > 0");

    /* Xác minh dữ liệu trong bảng questions */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT content, topic, difficulty, created_at FROM questions WHERE id = ?;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuẩn bị statement SELECT questions");
    int q_found = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, q_id_1);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            q_found = 1;
            const unsigned char *c = sqlite3_column_text(stmt, 0);
            const unsigned char *t = sqlite3_column_text(stmt, 1);
            const unsigned char *d = sqlite3_column_text(stmt, 2);
            const unsigned char *cat = sqlite3_column_text(stmt, 3);
            TEST_ASSERT(c && strcmp((const char *)c, "TCP hoạt động ở tầng nào trong OSI?") == 0, "A1c. content trong DB khớp chính xác");
            TEST_ASSERT(t && strcmp((const char *)t, "Network") == 0, "A1d. topic trong DB khớp chính xác");
            TEST_ASSERT(d && strcmp((const char *)d, "MEDIUM") == 0, "A1e. difficulty trong DB khớp chính xác");
            TEST_ASSERT(cat && strlen((const char *)cat) > 0, "A1f. created_at không rỗng");
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(q_found, "Tìm thấy bản ghi câu hỏi trong CSDL");

    /* Xác minh số đáp án và cờ is_correct trong bảng answers */
    rc = sqlite3_prepare_v2(db, "SELECT content, is_correct FROM answers WHERE question_id = ? ORDER BY id ASC;", -1, &stmt, NULL);
    int ans_count = 0;
    int corr_count = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, q_id_1);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            ans_count++;
            int ic = sqlite3_column_int(stmt, 1);
            if (ic == 1) corr_count++;
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(ans_count == 2, "A1g. Số lượng đáp án lưu trong CSDL là 2");
    TEST_ASSERT(corr_count == 1, "A1h. Có đúng 1 đáp án có is_correct = 1 trong CSDL");

    /* 2. Tạo câu hỏi với 4 đáp án */
    int q_id_2 = 0;
    const char *answers_4 =
        "["
        "  {\"content\": \"SELECT\", \"is_correct\": 0},"
        "  {\"content\": \"UPDATE\", \"is_correct\": 0},"
        "  {\"content\": \"DROP\", \"is_correct\": 1},"
        "  {\"content\": \"INSERT\", \"is_correct\": 0}"
        "]";
    rc = question_create(db, "Lệnh nào sau đây thuộc DDL trong SQL?", "Database", "EASY", answers_4, &q_id_2);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q_id_2 > 0, "A2a. Tạo câu hỏi 4 đáp án thành công");
    TEST_ASSERT(q_id_2 != q_id_1, "A2b. ID câu hỏi mới khác câu hỏi trước");

    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*), SUM(is_correct) FROM answers WHERE question_id = ?;", -1, &stmt, NULL);
    int total_a4 = 0, sum_c4 = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, q_id_2);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            total_a4 = sqlite3_column_int(stmt, 0);
            sum_c4 = sqlite3_column_int(stmt, 1);
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(total_a4 == 4, "A2c. Bảng answers có 4 bản ghi cho câu hỏi 2");
    TEST_ASSERT(sum_c4 == 1, "A2d. Có đúng 1 đáp án đúng cho câu hỏi 2");

    /* 3. Tạo nhiều câu hỏi liên tiếp, ID tăng dần không trùng */
    int q_id_3 = 0, q_id_4 = 0;
    rc = question_create(db, "Q3 content", "OS", "HARD", answers_2, &q_id_3);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q_id_3 > q_id_2, "A3a. Tạo câu hỏi 3 thành công, ID tăng dần");
    rc = question_create(db, "Q4 content", "OS", "HARD", answers_2, &q_id_4);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q_id_4 > q_id_3, "A3b. Tạo câu hỏi 4 thành công, ID tăng dần không trùng");
}

/* =========================================================================
 * NHÓM B — VALIDATION QUESTION_CREATE
 * ========================================================================= */
static void test_group_b_create_validation(sqlite3 *db)
{
    printf("\n--- Nhóm B: Validation question_create() ---\n");

    int q_before = count_rows(db, "questions");
    int a_before = count_rows(db, "answers");

    const char *valid_ans = "[{\"content\": \"A\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";
    int out_qid = 999;

    /* 1. db == NULL */
    int rc = question_create(NULL, "Content", "Topic", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B1. db == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 2. out_question_id == NULL */
    rc = question_create(db, "Content", "Topic", "EASY", valid_ans, NULL);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "B2. out_question_id == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 3. content, topic, difficulty, answers_json là NULL */
    out_qid = 999;
    rc = question_create(db, NULL, "Topic", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B3a. content == NULL trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", NULL, "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B3b. topic == NULL trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", NULL, valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B3c. difficulty == NULL trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", NULL, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B3d. answers_json == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 4. content hoặc topic rỗng hoặc chỉ có whitespace */
    out_qid = 999;
    rc = question_create(db, "", "Topic", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B4a. content rỗng trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "   \t \n  ", "Topic", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B4b. content chỉ có whitespace trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B4c. topic rỗng trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "   ", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B4d. topic chỉ có whitespace trả về QUESTION_ERR_INVALID_PARAM");

    /* 5. difficulty không thuộc EASY/MEDIUM/HARD */
    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "VERY_HARD", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B5a. difficulty 'VERY_HARD' trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B5b. difficulty rỗng trả về QUESTION_ERR_INVALID_PARAM");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "easy", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_qid == 0, "B5c. difficulty chữ thường 'easy' trả về QUESTION_ERR_INVALID_PARAM");

    /* 6. JSON sai cú pháp */
    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "{invalid_json", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B6. JSON sai cú pháp trả về QUESTION_ERR_INVALID_DATA");

    /* 7. JSON có ký tự thừa */
    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "[{\"content\": \"A\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}] trailing_garbage", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B7. JSON có ký tự thừa trả về QUESTION_ERR_INVALID_DATA");

    /* 8. JSON không phải array */
    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "{\"content\": \"A\", \"is_correct\": 1}", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B8a. JSON object trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "\"string_not_array\"", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B8b. JSON string trả về QUESTION_ERR_INVALID_DATA");

    /* 9. Array rỗng hoặc chỉ có 1 đáp án */
    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "[]", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B9a. Array rỗng trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    rc = question_create(db, "Content", "Topic", "EASY", "[{\"content\": \"A\", \"is_correct\": 1}]", &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B9b. Array chỉ có 1 đáp án trả về QUESTION_ERR_INVALID_DATA");

    /* 10. Không có đáp án đúng (0 correct) */
    out_qid = 999;
    const char *no_corr = "[{\"content\": \"A\", \"is_correct\": 0}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", no_corr, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B10. 0 đáp án đúng trả về QUESTION_ERR_INVALID_DATA");

    /* 11. Có nhiều hơn 1 đáp án đúng (2 correct) */
    out_qid = 999;
    const char *two_corr = "[{\"content\": \"A\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 1}]";
    rc = question_create(db, "Content", "Topic", "EASY", two_corr, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B11. 2 đáp án đúng trả về QUESTION_ERR_INVALID_DATA");

    /* 12. is_correct là boolean, string, float hoặc ngoài 0/1 */
    out_qid = 999;
    const char *bool_ans = "[{\"content\": \"A\", \"is_correct\": true}, {\"content\": \"B\", \"is_correct\": false}]";
    rc = question_create(db, "Content", "Topic", "EASY", bool_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B12a. is_correct là boolean trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *str_ans = "[{\"content\": \"A\", \"is_correct\": \"1\"}, {\"content\": \"B\", \"is_correct\": \"0\"}]";
    rc = question_create(db, "Content", "Topic", "EASY", str_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B12b. is_correct là string trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *float_ans = "[{\"content\": \"A\", \"is_correct\": 1.5}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", float_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B12c. is_correct là float trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *out_range_ans = "[{\"content\": \"A\", \"is_correct\": 2}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", out_range_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B12d. is_correct = 2 trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *neg_ans = "[{\"content\": \"A\", \"is_correct\": -1}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", neg_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B12e. is_correct = -1 trả về QUESTION_ERR_INVALID_DATA");

    /* 13. Nội dung đáp án rỗng hoặc chỉ có whitespace */
    out_qid = 999;
    const char *blank_ans1 = "[{\"content\": \"\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", blank_ans1, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B13a. Đáp án rỗng trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *blank_ans2 = "[{\"content\": \"   \t \", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", blank_ans2, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B13b. Đáp án chỉ có whitespace trả về QUESTION_ERR_INVALID_DATA");

    /* 14. Thiếu khóa content hoặc is_correct */
    out_qid = 999;
    const char *missing_c = "[{\"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", missing_c, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B14a. Thiếu khóa content trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *missing_ic = "[{\"content\": \"A\"}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", missing_ic, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B14b. Thiếu khóa is_correct trả về QUESTION_ERR_INVALID_DATA");

    /* 15. Khóa content hoặc is_correct trùng lặp trong object */
    out_qid = 999;
    const char *dup_c = "[{\"content\": \"A\", \"content\": \"Duplicate\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", dup_c, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B15a. Object có khóa content trùng lặp trả về QUESTION_ERR_INVALID_DATA");

    out_qid = 999;
    const char *dup_ic = "[{\"content\": \"A\", \"is_correct\": 1, \"is_correct\": 0}, {\"content\": \"B\", \"is_correct\": 0}]";
    rc = question_create(db, "Content", "Topic", "EASY", dup_ic, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_DATA && out_qid == 0, "B15b. Object có khóa is_correct trùng lặp trả về QUESTION_ERR_INVALID_DATA");

    /* Xác minh dữ liệu không bị ghi khi validation thất bại */
    int q_after = count_rows(db, "questions");
    int a_after = count_rows(db, "answers");
    TEST_ASSERT(q_before == q_after, "B16a. Bảng questions không bị thêm bản ghi nào khi validation thất bại");
    TEST_ASSERT(a_before == a_after, "B16b. Bảng answers không bị thêm bản ghi nào khi validation thất bại");
}

/* =========================================================================
 * NHÓM C — TRANSACTION VÀ ROLLBACK
 * ========================================================================= */
static void test_group_c_transaction_rollback(sqlite3 *db)
{
    printf("\n--- Nhóm C: Transaction và Rollback khi lỗi CSDL ---\n");

    int q_before = count_rows(db, "questions");
    int a_before = count_rows(db, "answers");

    /* Tạo trigger chặn INSERT vào bảng answers */
    const char *create_trigger_sql =
        "CREATE TRIGGER reject_answer_insert "
        "BEFORE INSERT ON answers "
        "BEGIN "
        "    SELECT RAISE(ABORT, 'forced answer insert failure'); "
        "END;";
    exec_sql(db, create_trigger_sql);

    int out_qid = 999;
    const char *valid_ans = "[{\"content\": \"Ans A\", \"is_correct\": 1}, {\"content\": \"Ans B\", \"is_correct\": 0}]";

    int rc = question_create(db, "Question triggering rollback", "Testing", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "C1. question_create() trả về QUESTION_ERR_DATABASE khi INSERT answers bị từ chối");
    TEST_ASSERT(out_qid == 0, "C2. out_question_id được gán về 0");

    /* Kiểm tra rollback hoàn toàn */
    int q_after = count_rows(db, "questions");
    int a_after = count_rows(db, "answers");
    TEST_ASSERT(q_before == q_after, "C3. Bản ghi question vừa tạo đã được ROLLBACK (không lưu dở dang)");
    TEST_ASSERT(a_before == a_after, "C4. Không có bản ghi answer nào được lưu");

    /* Xóa trigger và xác minh tạo lại thành công */
    exec_sql(db, "DROP TRIGGER reject_answer_insert;");

    out_qid = 0;
    rc = question_create(db, "Question after trigger drop", "Testing", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "C5a. Tạo câu hỏi thành công sau khi xóa trigger");
    TEST_ASSERT(out_qid > 0, "C5b. out_question_id hợp lệ (> 0)");
    TEST_ASSERT(count_rows(db, "questions") == q_before + 1, "C5c. Bảng questions ghi nhận đúng 1 bản ghi mới");
    TEST_ASSERT(count_rows(db, "answers") == a_before + 2, "C5d. Bảng answers ghi nhận đúng 2 đáp án mới");
}

/* =========================================================================
 * NHÓM D — QUESTION_GET_DETAIL THÀNH CÔNG
 * ========================================================================= */
static void test_group_d_get_detail_success(sqlite3 *db)
{
    printf("\n--- Nhóm D: question_get_detail() thành công ---\n");

    /* Tạo câu hỏi có Unicode và ký tự đặc biệt */
    int q_id = 0;
    const char *answers_unicode =
        "["
        "  {\"content\": \"Khóa chính (PRIMARY KEY) & Khóa ngoại (FOREIGN KEY)\", \"is_correct\": 1},"
        "  {\"content\": \"Chỉ số (INDEX) & Khung nhìn (VIEW) <tag> \\\"quote\\\"\", \"is_correct\": 0}"
        "]";
    int rc = question_create(db, "Cặp ràng buộc toàn vẹn nào trong SQLite đảm bảo quan hệ 1-N?", "Cơ Sở Dữ Liệu", "HARD", answers_unicode, &q_id);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q_id > 0, "Tạo câu hỏi có ký tự đặc biệt thành công");

    char *out_json = NULL;
    rc = question_get_detail(db, q_id, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "D1. question_get_detail() trả về QUESTION_SUCCESS");
    TEST_ASSERT(out_json != NULL, "D2. out_json không NULL");

    /* Parse JSON bằng cJSON */
    cJSON *parsed = cJSON_Parse(out_json);
    TEST_ASSERT(parsed != NULL, "D3. cJSON_Parse() phân tích thành công out_json");
    TEST_ASSERT(cJSON_IsObject(parsed), "D4. Root JSON là Object");

    /* Kiểm tra đầy đủ các trường */
    cJSON *j_qid = cJSON_GetObjectItemCaseSensitive(parsed, "question_id");
    cJSON *j_cnt = cJSON_GetObjectItemCaseSensitive(parsed, "content");
    cJSON *j_top = cJSON_GetObjectItemCaseSensitive(parsed, "topic");
    cJSON *j_dif = cJSON_GetObjectItemCaseSensitive(parsed, "difficulty");
    cJSON *j_cat = cJSON_GetObjectItemCaseSensitive(parsed, "created_at");
    cJSON *j_ans = cJSON_GetObjectItemCaseSensitive(parsed, "answers");

    TEST_ASSERT(j_qid && cJSON_IsNumber(j_qid) && j_qid->valueint == q_id, "D5a. question_id khớp chính xác");
    TEST_ASSERT(j_cnt && cJSON_IsString(j_cnt) && strcmp(j_cnt->valuestring, "Cặp ràng buộc toàn vẹn nào trong SQLite đảm bảo quan hệ 1-N?") == 0, "D5b. content Unicode được giữ nguyên vẹn");
    TEST_ASSERT(j_top && cJSON_IsString(j_top) && strcmp(j_top->valuestring, "Cơ Sở Dữ Liệu") == 0, "D5c. topic Unicode được giữ nguyên vẹn");
    TEST_ASSERT(j_dif && cJSON_IsString(j_dif) && strcmp(j_dif->valuestring, "HARD") == 0, "D5d. difficulty khớp 'HARD'");
    TEST_ASSERT(j_cat && cJSON_IsString(j_cat) && strlen(j_cat->valuestring) > 0, "D5e. created_at không rỗng");
    TEST_ASSERT(j_ans && cJSON_IsArray(j_ans) && cJSON_GetArraySize(j_ans) == 2, "D5f. answers là mảng có 2 phần tử");

    /* Kiểm tra đáp án sắp xếp theo ID tăng dần */
    cJSON *a0 = cJSON_GetArrayItem(j_ans, 0);
    cJSON *a1 = cJSON_GetArrayItem(j_ans, 1);
    cJSON *aid0 = cJSON_GetObjectItemCaseSensitive(a0, "answer_id");
    cJSON *aid1 = cJSON_GetObjectItemCaseSensitive(a1, "answer_id");
    cJSON *ac0 = cJSON_GetObjectItemCaseSensitive(a0, "content");
    cJSON *ac1 = cJSON_GetObjectItemCaseSensitive(a1, "content");
    cJSON *aic0 = cJSON_GetObjectItemCaseSensitive(a0, "is_correct");
    cJSON *aic1 = cJSON_GetObjectItemCaseSensitive(a1, "is_correct");

    TEST_ASSERT(aid0 && aid1 && aid0->valueint < aid1->valueint, "D6. Danh sách đáp án được sắp xếp theo answer_id tăng dần");
    TEST_ASSERT(aic0 && cJSON_IsNumber(aic0) && aic0->valueint == 1, "D7a. Đáp án 1 có is_correct = 1 (number)");
    TEST_ASSERT(aic1 && cJSON_IsNumber(aic1) && aic1->valueint == 0, "D7b. Đáp án 2 có is_correct = 0 (number)");
    TEST_ASSERT(ac0 && strcmp(ac0->valuestring, "Khóa chính (PRIMARY KEY) & Khóa ngoại (FOREIGN KEY)") == 0, "D8a. Đáp án 1 chứa ký tự đặc biệt đúng");
    TEST_ASSERT(ac1 && strcmp(ac1->valuestring, "Chỉ số (INDEX) & Khung nhìn (VIEW) <tag> \"quote\"") == 0, "D8b. Đáp án 2 chứa ký tự đặc biệt & quote đúng");

    cJSON_Delete(parsed);
    free(out_json);
}

/* =========================================================================
 * NHÓM E — TRƯỜNG HỢP BIÊN QUESTION_GET_DETAIL
 * ========================================================================= */
static void test_group_e_get_detail_edge_cases(sqlite3 *db)
{
    printf("\n--- Nhóm E: Trường hợp biên question_get_detail() ---\n");

    char *out_json = (char *)0x1234;

    /* 1. ID không tồn tại */
    int rc = question_get_detail(db, 999999, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_NOT_FOUND, "E1a. question_id không tồn tại trả về QUESTION_ERR_NOT_FOUND");
    TEST_ASSERT(out_json == NULL, "E1b. *out_json được đặt về NULL");

    /* 2. ID bằng 0 hoặc âm */
    out_json = (char *)0x1234;
    rc = question_get_detail(db, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_json == NULL, "E2a. question_id = 0 trả về QUESTION_ERR_INVALID_PARAM");

    out_json = (char *)0x1234;
    rc = question_get_detail(db, -10, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_json == NULL, "E2b. question_id < 0 trả về QUESTION_ERR_INVALID_PARAM");

    /* 3. db == NULL */
    out_json = (char *)0x1234;
    rc = question_get_detail(NULL, 1, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM && out_json == NULL, "E3. db == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 4. out_json == NULL */
    rc = question_get_detail(db, 1, NULL);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E4. out_json == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 5. Câu hỏi tồn tại nhưng không có đáp án -> trả mảng answers rỗng [] */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (8888, 'Câu hỏi chưa có đáp án', 'Test', 'EASY');");
    out_json = NULL;
    rc = question_get_detail(db, 8888, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "E5a. Đọc câu hỏi không có đáp án thành công");

    cJSON *parsed = cJSON_Parse(out_json);
    TEST_ASSERT(parsed != NULL, "E5b. Parse JSON thành công");
    cJSON *ans = cJSON_GetObjectItemCaseSensitive(parsed, "answers");
    TEST_ASSERT(ans && cJSON_IsArray(ans), "E5c. Trường answers là JSON array");
    TEST_ASSERT(cJSON_GetArraySize(ans) == 0, "E5d. Mảng answers rỗng (0 phần tử)");

    cJSON_Delete(parsed);
    free(out_json);
}

/* =========================================================================
 * NHÓM F — LỖI DATABASE
 * ========================================================================= */
static void test_group_f_database_error(void)
{
    printf("\n--- Nhóm F: Xử lý lỗi Database (thiếu bảng) ---\n");

    /* Mở database rỗng không có schema */
    sqlite3 *empty_db = NULL;
    int rc = sqlite3_open(":memory:", &empty_db);
    TEST_ASSERT(rc == SQLITE_OK && empty_db != NULL, "Mở database rỗng");

    int out_qid = 999;
    const char *valid_ans = "[{\"content\": \"A\", \"is_correct\": 1}, {\"content\": \"B\", \"is_correct\": 0}]";

    /* question_create() trên DB thiếu bảng */
    rc = question_create(empty_db, "Content", "Topic", "EASY", valid_ans, &out_qid);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "F1a. question_create() trả về QUESTION_ERR_DATABASE khi thiếu bảng");
    TEST_ASSERT(out_qid == 0, "F1b. out_question_id được đặt về 0");

    /* question_get_detail() trên DB thiếu bảng */
    char *out_json = (char *)0x1234;
    rc = question_get_detail(empty_db, 1, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_DATABASE, "F2a. question_get_detail() trả về QUESTION_ERR_DATABASE khi thiếu bảng");
    TEST_ASSERT(out_json == NULL, "F2b. *out_json được đặt về NULL");

    sqlite3_close(empty_db);
}

int main(void)
{
    printf("================================================================\n");
    printf("UNIT TESTS: QUESTION CREATE & GET DETAIL (TASK 05.3.3)\n");
    printf("================================================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "FATAL: Could not initialize SQLite test database.\n");
        return EXIT_FAILURE;
    }

    test_group_a_create_success(db);
    test_group_b_create_validation(db);
    test_group_c_transaction_rollback(db);
    test_group_d_get_detail_success(db);
    test_group_e_get_detail_edge_cases(db);
    test_group_f_database_error();

    sqlite3_close(db);

    printf("\n================================================================\n");
    printf("QUESTION TEST RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    if (g_tests_passed == g_tests_run && g_tests_run > 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

