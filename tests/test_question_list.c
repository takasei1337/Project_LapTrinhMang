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

static const char *FIXTURE_ANSWERS =
    "["
    "  {\"content\": \"Đáp án đúng\", \"is_correct\": 1},"
    "  {\"content\": \"Đáp án sai\", \"is_correct\": 0}"
    "]";

typedef struct {
    int id;
    const char *content;
    const char *topic;
    const char *difficulty;
} SeedQuestion;

static SeedQuestion g_seeds[] = {
    {0, "TCP hoạt động ở tầng nào trong mô hình OSI?", "Network", "EASY"},
    {0, "Cơ chế \"3-way handshake\" của TCP gồm những gói tin nào?", "Network", "MEDIUM"},
    {0, "Điều khiển tắc nghẽn TCP: <Slow Start & Congestion Avoidance>\nCó mấy thuật toán?", "Network", "HARD"},
    {0, "Khái niệm Deadlock trong Hệ điều hành & 4 điều kiện cần?", "OS", "EASY"},
    {0, "Thuật toán Banker dùng để làm gì?\nĐộ phức tạp O(m * n^2).", "OS", "MEDIUM"},
    {0, "Giải thuật lập lịch CFS (Completely Fair Scheduler) trong Linux Kernel?", "OS", "HARD"},
    {0, "Chỉ mục B-Tree và Hash Index khác nhau như thế nào trong CSDL?", "Database", "MEDIUM"},
    {0, "ACID: Tính nguyên tử (Atomicity) & tính cô lập (Isolation) bảo đảm điều gì?", "Database", "HARD"}
};
static const int NUM_SEEDS = sizeof(g_seeds) / sizeof(g_seeds[0]);

static void seed_questions(sqlite3 *db)
{
    for (int i = 0; i < NUM_SEEDS; i++) {
        int q_id = 0;
        int rc = question_create(db, g_seeds[i].content, g_seeds[i].topic, g_seeds[i].difficulty, FIXTURE_ANSWERS, &q_id);
        if (rc != QUESTION_SUCCESS || q_id <= 0) {
            fprintf(stderr, "Failed to seed question %d\n", i);
            exit(EXIT_FAILURE);
        }
        g_seeds[i].id = q_id;
    }
}

/* =========================================================================
 * NHÓM A — DANH SÁCH CƠ BẢN
 * ========================================================================= */
static void test_group_a_basic_list(void)
{
    printf("\n--- Nhóm A: Danh sách cơ bản ---\n");

    /* 1. Database không có câu hỏi */
    sqlite3 *db_empty = setup_test_db();
    char *out_json = NULL;
    int rc = question_list(db_empty, NULL, NULL, 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "A1a. DB rỗng trả về QUESTION_SUCCESS");
    TEST_ASSERT(out_json != NULL, "A1b. DB rỗng out_json không NULL");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        TEST_ASSERT(root != NULL && cJSON_IsObject(root), "A1c. Parse JSON thành công và root là object");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsArray(q_arr), "A1d. questions là JSON array");
        TEST_ASSERT(cJSON_GetArraySize(q_arr) == 0, "A1e. questions array rỗng (0 phần tử)");
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 0, "A1f. count = 0");
        cJSON *lim = cJSON_GetObjectItemCaseSensitive(root, "limit");
        TEST_ASSERT(cJSON_IsNumber(lim) && lim->valueint == 10, "A1g. limit = 10");
        cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "offset");
        TEST_ASSERT(cJSON_IsNumber(off) && off->valueint == 0, "A1h. offset = 0");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }
    sqlite3_close(db_empty);

    /* 2. Tạo 8 câu hỏi với nhiều chủ đề và độ khó */
    sqlite3 *db = setup_test_db();
    seed_questions(db);

    /* 3. Liệt kê toàn bộ khi topic == NULL, difficulty == NULL */
    rc = question_list(db, NULL, NULL, 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS, "A2a. Liệt kê toàn bộ trả về QUESTION_SUCCESS");
    TEST_ASSERT(out_json != NULL, "A2b. out_json không NULL khi thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        TEST_ASSERT(root != NULL && cJSON_IsObject(root), "A2c. Parse JSON thành công");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsArray(q_arr), "A2d. questions là mảng");
        TEST_ASSERT(cJSON_GetArraySize(q_arr) == NUM_SEEDS, "A2e. Số lượng câu hỏi trong mảng đúng bằng 8");
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == NUM_SEEDS, "A2f. count bằng đúng 8");
        cJSON *lim = cJSON_GetObjectItemCaseSensitive(root, "limit");
        TEST_ASSERT(cJSON_IsNumber(lim) && lim->valueint == 20, "A2g. limit phản ánh đúng tham số 20");
        cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "offset");
        TEST_ASSERT(cJSON_IsNumber(off) && off->valueint == 0, "A2h. offset phản ánh đúng tham số 0");

        /* Xác minh đầy đủ 5 trường trong từng câu hỏi */
        int fields_valid = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *item = cJSON_GetArrayItem(q_arr, i);
            cJSON *qid = cJSON_GetObjectItemCaseSensitive(item, "question_id");
            cJSON *cnt_item = cJSON_GetObjectItemCaseSensitive(item, "content");
            cJSON *top_item = cJSON_GetObjectItemCaseSensitive(item, "topic");
            cJSON *dif_item = cJSON_GetObjectItemCaseSensitive(item, "difficulty");
            cJSON *cre_item = cJSON_GetObjectItemCaseSensitive(item, "created_at");

            if (!cJSON_IsNumber(qid) || qid->valueint <= 0 ||
                !cJSON_IsString(cnt_item) || strlen(cnt_item->valuestring) == 0 ||
                !cJSON_IsString(top_item) || strlen(top_item->valuestring) == 0 ||
                !cJSON_IsString(dif_item) || strlen(dif_item->valuestring) == 0 ||
                !cJSON_IsString(cre_item) || strlen(cre_item->valuestring) == 0) {
                fields_valid = 0;
                break;
            }
        }
        TEST_ASSERT(fields_valid == 1, "A2i. Tất cả các câu hỏi có đầy đủ 5 trường hợp lệ");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }
    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM B — BỘ LỌC
 * ========================================================================= */
static void test_group_b_filters(void)
{
    printf("\n--- Nhóm B: Bộ lọc câu hỏi ---\n");
    sqlite3 *db = setup_test_db();
    seed_questions(db);

    char *out_json = NULL;

    /* 1. Lọc chỉ theo topic = "Network" (kỳ vọng 3 câu) */
    int rc = question_list(db, "Network", NULL, 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B1a. Lọc theo topic='Network' thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 3, "B1b. Số câu hỏi thuộc topic 'Network' là 3");
        int all_match = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *item = cJSON_GetArrayItem(q_arr, i);
            cJSON *t = cJSON_GetObjectItemCaseSensitive(item, "topic");
            if (!t || strcmp(t->valuestring, "Network") != 0) all_match = 0;
        }
        TEST_ASSERT(all_match == 1, "B1c. 100% các câu hỏi trả về có topic = 'Network'");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    /* 2. Lọc chỉ theo difficulty = "HARD" (kỳ vọng 3 câu: q3, q6, q8) */
    rc = question_list(db, NULL, "HARD", 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B2a. Lọc theo difficulty='HARD' thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 3, "B2b. Số câu hỏi difficulty 'HARD' là 3");
        int all_match = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *item = cJSON_GetArrayItem(q_arr, i);
            cJSON *d = cJSON_GetObjectItemCaseSensitive(item, "difficulty");
            if (!d || strcmp(d->valuestring, "HARD") != 0) all_match = 0;
        }
        TEST_ASSERT(all_match == 1, "B2c. 100% các câu hỏi trả về có difficulty = 'HARD'");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    /* 3. Lọc kết hợp topic = "OS" và difficulty = "HARD" (kỳ vọng 1 câu: q6) */
    rc = question_list(db, "OS", "HARD", 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B3a. Lọc kết hợp topic='OS' & difficulty='HARD' thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 1, "B3b. Số câu hỏi kết hợp OS & HARD là 1");
        cJSON *first = cJSON_GetArrayItem(q_arr, 0);
        cJSON *qid = cJSON_GetObjectItemCaseSensitive(first, "question_id");
        TEST_ASSERT(qid && qid->valueint == g_seeds[5].id, "B3c. question_id khớp chính xác câu hỏi seed q6");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    /* 4. Bộ lọc không khớp câu hỏi nào: topic = "Security" */
    rc = question_list(db, "Security", NULL, 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B4a. Lọc topic không tồn tại trả về QUESTION_SUCCESS");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 0, "B4b. count = 0 khi không khớp topic");
        TEST_ASSERT(cJSON_IsArray(q_arr) && cJSON_GetArraySize(q_arr) == 0, "B4c. questions = [] khi không khớp topic");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    /* 5. Bộ lọc không khớp: kết hợp topic = "Database" & difficulty = "EASY" */
    rc = question_list(db, "Database", "EASY", 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B5a. Lọc kết hợp không khớp trả về QUESTION_SUCCESS");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        TEST_ASSERT(cJSON_IsNumber(cnt) && cnt->valueint == 0, "B5b. count = 0 khi kết hợp không khớp");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    /* 6. Kiểm tra khớp chính xác (Exact match): "Net" và "Networking" không khớp "Network" */
    rc = question_list(db, "Net", NULL, 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B6a. Query với 'Net' thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        TEST_ASSERT(cnt && cnt->valueint == 0, "B6b. topic='Net' không khớp 'Network' (exact match)");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    rc = question_list(db, "Networking", NULL, 20, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "B6c. Query với 'Networking' thành công");
    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        TEST_ASSERT(cnt && cnt->valueint == 0, "B6d. topic='Networking' không khớp 'Network' (exact match)");
        cJSON_Delete(root);
        free(out_json);
        out_json = NULL;
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM C — PHÂN TRANG VÀ THỨ TỰ SẮP XẾP
 * ========================================================================= */
static void test_group_c_pagination_and_sorting(void)
{
    printf("\n--- Nhóm C: Phân trang và sắp xếp ---\n");
    sqlite3 *db = setup_test_db();
    seed_questions(db);

    char *p1 = NULL, *p2 = NULL, *p3 = NULL, *p4 = NULL;

    /* 1. Phân trang 4 trang liên tiếp với limit = 2 */
    int rc1 = question_list(db, NULL, NULL, 2, 0, &p1);
    int rc2 = question_list(db, NULL, NULL, 2, 2, &p2);
    int rc3 = question_list(db, NULL, NULL, 2, 4, &p3);
    int rc4 = question_list(db, NULL, NULL, 2, 6, &p4);

    TEST_ASSERT(rc1 == QUESTION_SUCCESS && p1 != NULL, "C1a. Trang 1 (limit=2, offset=0) thành công");
    TEST_ASSERT(rc2 == QUESTION_SUCCESS && p2 != NULL, "C1b. Trang 2 (limit=2, offset=2) thành công");
    TEST_ASSERT(rc3 == QUESTION_SUCCESS && p3 != NULL, "C1c. Trang 3 (limit=2, offset=4) thành công");
    TEST_ASSERT(rc4 == QUESTION_SUCCESS && p4 != NULL, "C1d. Trang 4 (limit=2, offset=6) thành công");

    if (p1 && p2 && p3 && p4) {
        cJSON *r1 = cJSON_Parse(p1);
        cJSON *r2 = cJSON_Parse(p2);
        cJSON *r3 = cJSON_Parse(p3);
        cJSON *r4 = cJSON_Parse(p4);

        cJSON *a1 = cJSON_GetObjectItemCaseSensitive(r1, "questions");
        cJSON *a2 = cJSON_GetObjectItemCaseSensitive(r2, "questions");
        cJSON *a3 = cJSON_GetObjectItemCaseSensitive(r3, "questions");
        cJSON *a4 = cJSON_GetObjectItemCaseSensitive(r4, "questions");

        TEST_ASSERT(cJSON_GetArraySize(a1) == 2, "C1e. Trang 1 chứa đúng 2 câu hỏi");
        TEST_ASSERT(cJSON_GetArraySize(a2) == 2, "C1f. Trang 2 chứa đúng 2 câu hỏi");
        TEST_ASSERT(cJSON_GetArraySize(a3) == 2, "C1g. Trang 3 chứa đúng 2 câu hỏi");
        TEST_ASSERT(cJSON_GetArraySize(a4) == 2, "C1h. Trang 4 chứa đúng 2 câu hỏi");

        int ids[8];
        ids[0] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a1, 0), "question_id")->valueint;
        ids[1] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a1, 1), "question_id")->valueint;
        ids[2] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a2, 0), "question_id")->valueint;
        ids[3] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a2, 1), "question_id")->valueint;
        ids[4] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a3, 0), "question_id")->valueint;
        ids[5] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a3, 1), "question_id")->valueint;
        ids[6] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a4, 0), "question_id")->valueint;
        ids[7] = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(a4, 1), "question_id")->valueint;

        /* Kiểm tra không trùng lặp giữa các trang */
        int no_duplicates = 1;
        for (int i = 0; i < 8; i++) {
            for (int j = i + 1; j < 8; j++) {
                if (ids[i] == ids[j]) no_duplicates = 0;
            }
        }
        TEST_ASSERT(no_duplicates == 1, "C1i. Các trang phân trang không có câu hỏi trùng lặp");

        /* Kiểm tra sắp xếp tăng dần nghiêm ngặt */
        int strictly_increasing = 1;
        for (int i = 0; i < 7; i++) {
            if (ids[i] >= ids[i + 1]) strictly_increasing = 0;
        }
        TEST_ASSERT(strictly_increasing == 1, "C1j. ID các câu hỏi tăng dần nghiêm ngặt (ORDER BY id ASC)");

        cJSON_Delete(r1);
        cJSON_Delete(r2);
        cJSON_Delete(r3);
        cJSON_Delete(r4);
    }
    free(p1); free(p2); free(p3); free(p4);

    /* 2. Offset vượt quá số lượng câu hỏi */
    char *out_over = NULL;
    int rc_over = question_list(db, NULL, NULL, 10, 100, &out_over);
    TEST_ASSERT(rc_over == QUESTION_SUCCESS && out_over != NULL, "C2a. Offset vượt quá số câu hỏi trả về QUESTION_SUCCESS");
    if (out_over) {
        cJSON *root = cJSON_Parse(out_over);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "offset");
        TEST_ASSERT(cnt && cnt->valueint == 0, "C2b. count = 0 khi offset vượt số câu");
        TEST_ASSERT(cJSON_GetArraySize(q_arr) == 0, "C2c. questions = [] khi offset vượt số câu");
        TEST_ASSERT(off && off->valueint == 100, "C2d. offset trong JSON giữ nguyên 100");
        cJSON_Delete(root);
        free(out_over);
    }

    /* 3. Biên limit = 1 */
    char *out_lim1 = NULL;
    int rc_lim1 = question_list(db, NULL, NULL, 1, 0, &out_lim1);
    TEST_ASSERT(rc_lim1 == QUESTION_SUCCESS && out_lim1 != NULL, "C3a. limit = 1 trả về QUESTION_SUCCESS");
    if (out_lim1) {
        cJSON *root = cJSON_Parse(out_lim1);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cnt && cnt->valueint == 1, "C3b. count = 1 khi limit = 1");
        TEST_ASSERT(cJSON_GetArraySize(q_arr) == 1, "C3c. questions array có đúng 1 phần tử");
        cJSON_Delete(root);
        free(out_lim1);
    }

    /* 4. Biên limit = 100 */
    char *out_lim100 = NULL;
    int rc_lim100 = question_list(db, NULL, NULL, 100, 0, &out_lim100);
    TEST_ASSERT(rc_lim100 == QUESTION_SUCCESS && out_lim100 != NULL, "C4a. limit = 100 trả về QUESTION_SUCCESS");
    if (out_lim100) {
        cJSON *root = cJSON_Parse(out_lim100);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *lim = cJSON_GetObjectItemCaseSensitive(root, "limit");
        TEST_ASSERT(cnt && cnt->valueint == 8, "C4b. count = 8 (toàn bộ)");
        TEST_ASSERT(lim && lim->valueint == 100, "C4c. limit = 100");
        cJSON_Delete(root);
        free(out_lim100);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM D — BẢO MẬT JSON VÀ KIỂM TRA KIỂU DỮ LIỆU
 * ========================================================================= */
static void test_group_d_json_security_and_types(void)
{
    printf("\n--- Nhóm D: Bảo mật JSON và kiểu dữ liệu ---\n");
    sqlite3 *db = setup_test_db();
    seed_questions(db);

    char *out_json = NULL;
    int rc = question_list(db, NULL, NULL, 50, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "D1. Gọi question_list thành công");

    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        TEST_ASSERT(root != NULL && cJSON_IsObject(root), "D2a. Root JSON là Object");

        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(root, "count");
        cJSON *lim = cJSON_GetObjectItemCaseSensitive(root, "limit");
        cJSON *off = cJSON_GetObjectItemCaseSensitive(root, "offset");

        TEST_ASSERT(cJSON_IsArray(q_arr), "D2b. 'questions' là Array");
        TEST_ASSERT(cJSON_IsNumber(cnt), "D2c. 'count' là Number");
        TEST_ASSERT(cJSON_IsNumber(lim), "D2d. 'limit' là Number");
        TEST_ASSERT(cJSON_IsNumber(off), "D2e. 'offset' là Number");

        int safe_from_answers = 1;
        int safe_from_is_correct = 1;
        int safe_from_correct_id = 1;
        int item_types_valid = 1;
        int extra_keys_count = 0;

        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *item = cJSON_GetArrayItem(q_arr, i);
            if (!cJSON_IsObject(item)) {
                item_types_valid = 0;
                continue;
            }

            /* Duyệt từng trường con trong item để kiểm tra rò rỉ và key lạ */
            int key_count = 0;
            for (cJSON *child = item->child; child != NULL; child = child->next) {
                key_count++;
                if (strcmp(child->string, "answers") == 0) {
                    safe_from_answers = 0;
                }
                if (strcmp(child->string, "is_correct") == 0) {
                    safe_from_is_correct = 0;
                }
                if (strcmp(child->string, "correct_answer_id") == 0) {
                    safe_from_correct_id = 0;
                }
            }
            if (key_count != 5) {
                extra_keys_count++;
            }

            cJSON *qid = cJSON_GetObjectItemCaseSensitive(item, "question_id");
            cJSON *content = cJSON_GetObjectItemCaseSensitive(item, "content");
            cJSON *topic = cJSON_GetObjectItemCaseSensitive(item, "topic");
            cJSON *difficulty = cJSON_GetObjectItemCaseSensitive(item, "difficulty");
            cJSON *created_at = cJSON_GetObjectItemCaseSensitive(item, "created_at");

            if (!cJSON_IsNumber(qid) || !cJSON_IsString(content) ||
                !cJSON_IsString(topic) || !cJSON_IsString(difficulty) ||
                !cJSON_IsString(created_at)) {
                item_types_valid = 0;
            }
        }

        TEST_ASSERT(safe_from_answers == 1, "D3a. Không có khóa 'answers' trong bất kỳ phần tử nào");
        TEST_ASSERT(safe_from_is_correct == 1, "D3b. Không có khóa 'is_correct' trong bất kỳ phần tử nào");
        TEST_ASSERT(safe_from_correct_id == 1, "D3c. Không có khóa 'correct_answer_id' trong bất kỳ phần tử nào");
        TEST_ASSERT(item_types_valid == 1, "D4. Tất cả các trường của phần tử có kiểu dữ liệu chính xác");
        TEST_ASSERT(extra_keys_count == 0, "D5. Không có trường lạ nào ngoài 5 trường quy định");

        cJSON_Delete(root);
        free(out_json);
    }

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM E — VALIDATION THAM SỐ
 * ========================================================================= */
static void test_group_e_validation(void)
{
    printf("\n--- Nhóm E: Validation tham số ---\n");
    sqlite3 *db = setup_test_db();
    char *out_json = (char *)0x1234;

    /* 1. db == NULL */
    int rc = question_list(NULL, NULL, NULL, 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E1a. db == NULL trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E1b. *out_json được đặt về NULL khi db == NULL");

    /* 2. out_json == NULL */
    rc = question_list(db, NULL, NULL, 10, 0, NULL);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E2. out_json == NULL trả về QUESTION_ERR_INVALID_PARAM");

    /* 3. topic rỗng */
    out_json = (char *)0x1234;
    rc = question_list(db, "", NULL, 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E3a. topic rỗng trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E3b. *out_json = NULL khi topic rỗng");

    /* 4. topic chỉ chứa whitespace */
    out_json = (char *)0x1234;
    rc = question_list(db, "   \t\n  ", NULL, 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E4a. topic whitespace trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E4b. *out_json = NULL khi topic whitespace");

    /* 5. difficulty rỗng */
    out_json = (char *)0x1234;
    rc = question_list(db, NULL, "", 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E5a. difficulty rỗng trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E5b. *out_json = NULL khi difficulty rỗng");

    /* 6. difficulty sai giá trị */
    out_json = (char *)0x1234;
    rc = question_list(db, NULL, "VERY_HARD", 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E6a. difficulty 'VERY_HARD' trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E6b. *out_json = NULL");

    out_json = (char *)0x1234;
    rc = question_list(db, NULL, "easy", 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E6c. difficulty chữ thường 'easy' trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E6d. *out_json = NULL");

    /* 7. limit sai biên */
    out_json = (char *)0x1234;
    rc = question_list(db, NULL, NULL, 0, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E7a. limit = 0 trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E7b. *out_json = NULL khi limit = 0");

    out_json = (char *)0x1234;
    rc = question_list(db, NULL, NULL, -1, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E7c. limit = -1 trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E7d. *out_json = NULL khi limit = -1");

    out_json = (char *)0x1234;
    rc = question_list(db, NULL, NULL, 101, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E7e. limit = 101 trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E7f. *out_json = NULL khi limit = 101");

    /* 8. offset âm */
    out_json = (char *)0x1234;
    rc = question_list(db, NULL, NULL, 10, -1, &out_json);
    TEST_ASSERT(rc == QUESTION_ERR_INVALID_PARAM, "E8a. offset = -1 trả về QUESTION_ERR_INVALID_PARAM");
    TEST_ASSERT(out_json == NULL, "E8b. *out_json = NULL khi offset = -1");

    sqlite3_close(db);
}

/* =========================================================================
 * NHÓM F — LỖI SQLITE
 * ========================================================================= */
static void test_group_f_sqlite_errors(void)
{
    printf("\n--- Nhóm F: Xử lý lỗi SQLite ---\n");

    /* 1. Database thiếu bảng questions */
    sqlite3 *empty_db = NULL;
    int rc = sqlite3_open(":memory:", &empty_db);
    TEST_ASSERT(rc == SQLITE_OK && empty_db != NULL, "F1a. Mở in-memory DB trắng thành công");

    char *out_json = (char *)0x1234;
    int q_rc = question_list(empty_db, NULL, NULL, 10, 0, &out_json);
    TEST_ASSERT(q_rc == QUESTION_ERR_DATABASE, "F1b. Thiếu bảng questions trả về QUESTION_ERR_DATABASE");
    TEST_ASSERT(out_json == NULL, "F1c. *out_json được gán NULL khi xảy ra lỗi DB");
    sqlite3_close(empty_db);

    /* 2. Database bị đóng trước khi truy vấn */
    sqlite3 *closed_db = setup_test_db();
    sqlite3_close(closed_db);
    out_json = (char *)0x1234;
    q_rc = question_list(closed_db, NULL, NULL, 10, 0, &out_json);
    TEST_ASSERT(q_rc == QUESTION_ERR_DATABASE, "F2a. Gọi trên DB đã đóng trả về QUESTION_ERR_DATABASE");
    TEST_ASSERT(out_json == NULL, "F2b. *out_json được gán NULL");
}

/* =========================================================================
 * MỤC 4 — KIỂM TRA UNICODE VÀ KÝ TỰ ĐẶC BIỆT
 * ========================================================================= */
static void test_group_unicode_and_special_chars(void)
{
    printf("\n--- Mục 4: Unicode và ký tự đặc biệt ---\n");
    sqlite3 *db = setup_test_db();

    const char *special_content =
        "Giao thức \"TCP/IP\" & <QUIC>:\n"
        "1. TCP có kiểm soát luồng không?\n"
        "2. Ký tự đặc biệt: 'nháy đơn', \"nháy kép\", <tag>, &amp;, xuống dòng \\n.";
    const char *special_topic = "Mạng & Giao thức";

    int q_id = 0;
    int rc = question_create(db, special_content, special_topic, "HARD", FIXTURE_ANSWERS, &q_id);
    TEST_ASSERT(rc == QUESTION_SUCCESS && q_id > 0, "U1. Tạo câu hỏi chứa Unicode và ký tự đặc biệt thành công");

    char *out_json = NULL;
    rc = question_list(db, special_topic, "HARD", 10, 0, &out_json);
    TEST_ASSERT(rc == QUESTION_SUCCESS && out_json != NULL, "U2a. Lọc câu hỏi theo topic Unicode thành công");

    if (out_json) {
        cJSON *root = cJSON_Parse(out_json);
        TEST_ASSERT(root != NULL, "U2b. Parse JSON trả về thành công");
        cJSON *q_arr = cJSON_GetObjectItemCaseSensitive(root, "questions");
        TEST_ASSERT(cJSON_GetArraySize(q_arr) == 1, "U2c. Tìm thấy đúng 1 câu hỏi");

        cJSON *item = cJSON_GetArrayItem(q_arr, 0);
        cJSON *cnt = cJSON_GetObjectItemCaseSensitive(item, "content");
        cJSON *top = cJSON_GetObjectItemCaseSensitive(item, "topic");

        TEST_ASSERT(cnt && strcmp(cnt->valuestring, special_content) == 0,
                    "U3. Nội dung Unicode, nháy kép, xuống dòng, <>& giữ nguyên vẹn 100%");
        TEST_ASSERT(top && strcmp(top->valuestring, special_topic) == 0,
                    "U4. Chủ đề Unicode tiếng Việt giữ nguyên vẹn 100%");

        cJSON_Delete(root);
        free(out_json);
    }

    sqlite3_close(db);
}

int main(void)
{
    printf("================================================================\n");
    printf("UNIT TESTS: QUESTION_LIST() (TASK 05.4.2)\n");
    printf("================================================================\n");

    test_group_a_basic_list();
    test_group_b_filters();
    test_group_c_pagination_and_sorting();
    test_group_d_json_security_and_types();
    test_group_e_validation();
    test_group_f_sqlite_errors();
    test_group_unicode_and_special_chars();

    printf("\n================================================================\n");
    printf("QUESTION_LIST TEST RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}

