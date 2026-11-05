#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "classification.h"
#include "question_selector.h"

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

static sqlite3 *create_in_memory_db(void)
{
    sqlite3 *db = NULL;
    if (sqlite3_open(":memory:", &db) != SQLITE_OK) {
        return NULL;
    }
    char *err = NULL;
    sqlite3_exec(db, "PRAGMA foreign_keys = ON;", NULL, NULL, &err);
    if (err) sqlite3_free(err);

    char *schema = read_file_to_string("database/schema.sql");
    if (!schema) {
        sqlite3_close(db);
        return NULL;
    }
    int rc = sqlite3_exec(db, schema, NULL, NULL, &err);
    free(schema);
    if (err) sqlite3_free(err);
    if (rc != SQLITE_OK) {
        sqlite3_close(db);
        return NULL;
    }
    return db;
}

static int count_fk_violations(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    int violations = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        violations++;
    }
    sqlite3_finalize(stmt);
    return violations;
}

/* Helper tạo câu hỏi kèm đáp án kiểm soát được tính hợp lệ */
static int create_test_question(sqlite3 *db, int topic_id, const char *content,
                                const char *diff, int num_answers, int num_correct)
{
    const char *q_sql = "INSERT INTO questions (content, topic, difficulty, topic_id) VALUES (?, 'TestTopic', ?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, q_sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, diff, -1, SQLITE_STATIC);
    if (topic_id > 0) {
        sqlite3_bind_int(stmt, 3, topic_id);
    } else {
        sqlite3_bind_null(stmt, 3);
    }

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int qid = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);

    /* Chèn đáp án */
    const char *a_sql = "INSERT INTO answers (question_id, content, is_correct) VALUES (?, ?, ?);";
    for (int i = 0; i < num_answers; i++) {
        if (sqlite3_prepare_v2(db, a_sql, -1, &stmt, NULL) != SQLITE_OK) {
            return -1;
        }
        sqlite3_bind_int(stmt, 1, qid);
        char ans_buf[64];
        snprintf(ans_buf, sizeof(ans_buf), "Dap an %d cho Q%d", i + 1, qid);
        sqlite3_bind_text(stmt, 2, ans_buf, -1, SQLITE_STATIC);
        int corr = (i < num_correct) ? 1 : 0;
        sqlite3_bind_int(stmt, 3, corr);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return qid;
}

/*
 * ============================================================================
 * TEST 1, 2: MỘT TOPIC & NHIỀU TOPIC CÓ SỐ CÂU HỎI BẰNG NHAU
 * ============================================================================
 */
static void test_one_and_equal_topics(void)
{
    printf("\n=== KIỂM THỬ 1 & 2: Một Topic & Nhiều Topic bằng nhau ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top1 = 0, top2 = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);
    classification_topic_create(db, les, "Topic 1", NULL, &top1);
    classification_topic_create(db, les, "Topic 2", NULL, &top2);

    /* Chèn 5 câu hợp lệ cho Topic 1 */
    for (int i = 1; i <= 5; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T1 Q%d", i);
        create_test_question(db, top1, buf, "EASY", 4, 1);
    }

    /* 1. Một Topic duy nhất (chưa có câu nào ở top2) */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 3, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "1a. Chọn 3 câu từ 1 Topic thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *req = cJSON_GetObjectItem(root, "requested");
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *sht = cJSON_GetObjectItem(root, "shortage");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(req && req->valueint == 3, "1b. requested = 3");
            TEST_ASSERT(sel && sel->valueint == 3, "1c. selected = 3");
            TEST_ASSERT(sht && sht->valueint == 0, "1d. shortage = 0");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 3, "1e. Array size = 3");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Thêm 5 câu hợp lệ cho Topic 2 -> Hai topic bằng nhau (5 câu mỗi topic) */
    for (int i = 1; i <= 5; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "T2 Q%d", i);
        create_test_question(db, top2, buf, "EASY", 4, 1);
    }

    /* Yêu cầu 6 câu từ 2 topic bằng nhau -> Mỗi topic phải được đúng 3 câu */
    json = NULL;
    rc = question_selector_balanced(db, sub, chap, les, NULL, 6, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "2a. Chọn 6 câu từ 2 Topic cân bằng thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *dist = cJSON_GetObjectItem(root, "topic_distribution");
            TEST_ASSERT(dist && cJSON_GetArraySize(dist) == 2, "2b. topic_distribution có 2 phần tử");
            if (dist && cJSON_GetArraySize(dist) == 2) {
                cJSON *d1 = cJSON_GetArrayItem(dist, 0);
                cJSON *d2 = cJSON_GetArrayItem(dist, 1);
                cJSON *cnt1 = cJSON_GetObjectItem(d1, "count");
                cJSON *cnt2 = cJSON_GetObjectItem(d2, "count");
                TEST_ASSERT(cnt1 && cnt1->valueint == 3, "2c. Topic 1 được phân bổ đúng 3 câu");
                TEST_ASSERT(cnt2 && cnt2->valueint == 3, "2d. Topic 2 được phân bổ đúng 3 câu");
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 3, 7: TOPIC A CÓ 100 CÂU, TOPIC B CÓ 2 CÂU (ĐIỀU HÒA THIẾU HỤT)
 * ============================================================================
 */
static void test_imbalanced_pools_and_redistribution(void)
{
    printf("\n=== KIỂM THỬ 3 & 7: Topic A 100 câu, Topic B 2 câu & Chuyển thiếu hụt ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top_a = 0, top_b = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);
    classification_topic_create(db, les, "Topic A (100 câu)", NULL, &top_a);
    classification_topic_create(db, les, "Topic B (2 câu)", NULL, &top_b);

    /* Chèn 100 câu cho Topic A */
    for (int i = 1; i <= 100; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TA Q%d", i);
        create_test_question(db, top_a, buf, "MEDIUM", 4, 1);
    }

    /* Chèn 2 câu cho Topic B */
    create_test_question(db, top_b, "TB Q1", "MEDIUM", 4, 1);
    create_test_question(db, top_b, "TB Q2", "MEDIUM", 4, 1);

    /* Yêu cầu N = 10 câu:
     * Khởi điểm phân phối đều 5-5. Nhưng Topic B chỉ có 2 câu -> Thiếu 3 câu.
     * Phần thiếu 3 câu phải được tự động chuyển sang Topic A (2 + 8 = 10).
     */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "3a. Chọn 10 câu với nguồn lệch thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *sht = cJSON_GetObjectItem(root, "shortage");
            cJSON *dist = cJSON_GetObjectItem(root, "topic_distribution");
            TEST_ASSERT(sel && sel->valueint == 10, "3b. selected = 10");
            TEST_ASSERT(sht && sht->valueint == 0, "3c. shortage = 0");

            int cnt_a = 0, cnt_b = 0;
            if (dist) {
                int sz = cJSON_GetArraySize(dist);
                for (int i = 0; i < sz; i++) {
                    cJSON *d = cJSON_GetArrayItem(dist, i);
                    cJSON *tid = cJSON_GetObjectItem(d, "topic_id");
                    cJSON *cnt = cJSON_GetObjectItem(d, "count");
                    if (tid && tid->valueint == top_a) cnt_a = cnt->valueint;
                    if (tid && tid->valueint == top_b) cnt_b = cnt->valueint;
                }
            }
            TEST_ASSERT(cnt_b == 2, "3d. Topic B được lấy cạn toàn bộ 2 câu (không bỏ sót)");
            TEST_ASSERT(cnt_a == 8, "3e. Topic A nhận 8 câu (5 quota gốc + 3 bù thiếu hụt)");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 4, 5, 6: N < K, N = K, N > K
 * ============================================================================
 */
static void test_n_vs_k_scenarios(void)
{
    printf("\n=== KIỂM THỬ 4, 5, 6: N < K, N = K, N > K ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);

    /* Tạo K = 5 Topics, mỗi Topic 10 câu */
    int tids[5];
    for (int i = 0; i < 5; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Topic %d", i + 1);
        classification_topic_create(db, les, name, NULL, &tids[i]);
        for (int q = 1; q <= 10; q++) {
            char qbuf[64];
            snprintf(qbuf, sizeof(qbuf), "T%d Q%d", i + 1, q);
            create_test_question(db, tids[i], qbuf, "EASY", 4, 1);
        }
    }

    /* 1. N < K: N = 3, K = 5 -> Phải chọn đúng 3 Topic khác nhau, mỗi Topic đúng 1 câu */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 3, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "4a. N < K (N=3, K=5) thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *dist = cJSON_GetObjectItem(root, "topic_distribution");
            TEST_ASSERT(sel && sel->valueint == 3, "4b. selected = 3");
            TEST_ASSERT(dist && cJSON_GetArraySize(dist) == 3, "4c. Đúng 3 topic được chọn");
            if (dist) {
                int sz = cJSON_GetArraySize(dist);
                for (int i = 0; i < sz; i++) {
                    cJSON *d = cJSON_GetArrayItem(dist, i);
                    cJSON *cnt = cJSON_GetObjectItem(d, "count");
                    TEST_ASSERT(cnt && cnt->valueint == 1, "4d. Mỗi topic được chọn đúng 1 câu");
                }
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. N = K: N = 5, K = 5 -> Cả 5 Topic đều được chọn, mỗi Topic đúng 1 câu */
    json = NULL;
    rc = question_selector_balanced(db, sub, chap, les, NULL, 5, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "5a. N = K (N=5, K=5) thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *dist = cJSON_GetObjectItem(root, "topic_distribution");
            TEST_ASSERT(sel && sel->valueint == 5, "5b. selected = 5");
            TEST_ASSERT(dist && cJSON_GetArraySize(dist) == 5, "5c. Cả 5 topic đều tham gia");
            if (dist) {
                int sz = cJSON_GetArraySize(dist);
                for (int i = 0; i < sz; i++) {
                    cJSON *d = cJSON_GetArrayItem(dist, i);
                    cJSON *cnt = cJSON_GetObjectItem(d, "count");
                    TEST_ASSERT(cnt && cnt->valueint == 1, "5d. Mỗi topic đúng 1 câu");
                }
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 3. N > K: N = 12, K = 5 -> Quota cơ sở là 2 câu/topic, 2 topic nhận thêm 1 câu (3-3-2-2-2) */
    json = NULL;
    rc = question_selector_balanced(db, sub, chap, les, NULL, 12, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "6a. N > K (N=12, K=5) thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *dist = cJSON_GetObjectItem(root, "topic_distribution");
            TEST_ASSERT(sel && sel->valueint == 12, "6b. selected = 12");
            int count_3 = 0, count_2 = 0;
            if (dist) {
                int sz = cJSON_GetArraySize(dist);
                TEST_ASSERT(sz == 5, "6c. Toàn bộ 5 topic đều được phân bổ");
                for (int i = 0; i < sz; i++) {
                    cJSON *d = cJSON_GetArrayItem(dist, i);
                    cJSON *cnt = cJSON_GetObjectItem(d, "count");
                    if (cnt && cnt->valueint == 3) count_3++;
                    if (cnt && cnt->valueint == 2) count_2++;
                }
            }
            TEST_ASSERT(count_3 == 2, "6d. Đúng 2 topic nhận 3 câu");
            TEST_ASSERT(count_2 == 3, "6e. Đúng 3 topic nhận 2 câu");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 8, 9: TỔNG NGUỒN ÍT HƠN N & KHÔNG CÓ CÂU HỎI PHÙ HỢP
 * ============================================================================
 */
static void test_shortage_and_empty_scenarios(void)
{
    printf("\n=== KIỂM THỬ 8 & 9: Thiếu hụt nguồn & Không có câu hỏi ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);
    classification_topic_create(db, les, "Topic 1", NULL, &top);

    /* 1. Không có câu hỏi nào trong DB */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "9a. Selector khi DB rỗng trả về SUCCESS");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *req = cJSON_GetObjectItem(root, "requested");
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *sht = cJSON_GetObjectItem(root, "shortage");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(req && req->valueint == 10, "9b. requested = 10");
            TEST_ASSERT(sel && sel->valueint == 0, "9c. selected = 0");
            TEST_ASSERT(sht && sht->valueint == 10, "9d. shortage = 10");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 0, "9e. questions rỗng []");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Chỉ có 3 câu hợp lệ nhưng yêu cầu N = 10 */
    for (int i = 1; i <= 3; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Q%d", i);
        create_test_question(db, top, buf, "HARD", 4, 1);
    }

    json = NULL;
    rc = question_selector_balanced(db, sub, chap, les, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "8a. Tổng nguồn (3) < N (10) thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *req = cJSON_GetObjectItem(root, "requested");
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *sht = cJSON_GetObjectItem(root, "shortage");
            TEST_ASSERT(req && req->valueint == 10, "8b. requested = 10");
            TEST_ASSERT(sel && sel->valueint == 3, "8c. selected = 3 (thực tế)");
            TEST_ASSERT(sht && sht->valueint == 7, "8d. shortage = 7 (báo thiếu hụt chính xác)");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 10, 11, 12, 13: BỘ LỌC SUBJECT, CHAPTER, LESSON, DIFFICULTY
 * ============================================================================
 */
static void test_hierarchical_and_difficulty_filters(void)
{
    printf("\n=== KIỂM THỬ 10-13: Lọc Subject, Chapter, Lesson, Difficulty ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub1 = 0, sub2 = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub1);
    classification_subject_create(db, "S2", "Mon 2", NULL, &sub2);

    int c1 = 0, c2 = 0;
    classification_chapter_create(db, sub1, 1, "C1", NULL, &c1);
    classification_chapter_create(db, sub2, 1, "C2", NULL, &c2);

    int l1 = 0, l2 = 0;
    classification_lesson_create(db, c1, 1, "L1", NULL, &l1);
    classification_lesson_create(db, c2, 1, "L2", NULL, &l2);

    int t1 = 0, t2 = 0;
    classification_topic_create(db, l1, "Topic 1", NULL, &t1);
    classification_topic_create(db, l2, "Topic 2", NULL, &t2);

    /* Chèn câu hỏi cho Topic 1 (Sub 1): 2 câu EASY, 2 câu HARD */
    create_test_question(db, t1, "S1 T1 Q1 Easy", "EASY", 4, 1);
    create_test_question(db, t1, "S1 T1 Q2 Easy", "EASY", 4, 1);
    create_test_question(db, t1, "S1 T1 Q3 Hard", "HARD", 4, 1);
    create_test_question(db, t1, "S1 T1 Q4 Hard", "HARD", 4, 1);

    /* Chèn câu hỏi cho Topic 2 (Sub 2): 3 câu EASY */
    create_test_question(db, t2, "S2 T2 Q1 Easy", "EASY", 4, 1);
    create_test_question(db, t2, "S2 T2 Q2 Easy", "EASY", 4, 1);
    create_test_question(db, t2, "S2 T2 Q3 Easy", "EASY", 4, 1);

    /* 1. Lọc theo Subject 1 */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub1, 0, 0, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "10a. Filter theo Subject 1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            TEST_ASSERT(sel && sel->valueint == 4, "10b. Subject 1 có đúng 4 câu hỏi");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Lọc theo Chapter 2 (thuộc Sub 2) */
    json = NULL;
    rc = question_selector_balanced(db, 0, c2, 0, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "11a. Filter theo Chapter 2 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            TEST_ASSERT(sel && sel->valueint == 3, "11b. Chapter 2 có đúng 3 câu hỏi");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 3. Lọc theo Lesson 1 (thuộc Sub 1) */
    json = NULL;
    rc = question_selector_balanced(db, 0, 0, l1, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "12a. Filter theo Lesson 1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            TEST_ASSERT(sel && sel->valueint == 4, "12b. Lesson 1 có đúng 4 câu hỏi");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 4. Lọc theo Subject 1 + Difficulty EASY */
    json = NULL;
    rc = question_selector_balanced(db, sub1, 0, 0, "EASY", 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "13a. Filter Sub 1 + EASY thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            TEST_ASSERT(sel && sel->valueint == 2, "13b. Đúng 2 câu EASY trong Sub 1");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 5. Lọc theo Subject 1 + Difficulty HARD */
    json = NULL;
    rc = question_selector_balanced(db, sub1, 0, 0, "HARD", 10, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            TEST_ASSERT(sel && sel->valueint == 2, "13c. Đúng 2 câu HARD trong Sub 1");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 14, 15: LOẠI TRỪ CÂU HỎI KHÔNG HỢP LỆ (THIẾU ĐÁP ÁN HOẶC >1 ĐÁP ÁN ĐÚNG)
 * ============================================================================
 */
static void test_invalid_questions_exclusion(void)
{
    printf("\n=== KIỂM THỬ 14 & 15: Loại trừ Câu hỏi không hợp lệ ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);
    classification_topic_create(db, les, "Topic 1", NULL, &top);

    /* 1. Câu hỏi hợp lệ: 4 đáp án, 1 đúng */
    int q_valid = create_test_question(db, top, "Q Hop le", "EASY", 4, 1);

    /* 2. Câu hỏi thiếu đáp án: chỉ có 1 đáp án */
    int q_short_ans = create_test_question(db, top, "Q Thieu dap an", "EASY", 1, 1);

    /* 3. Câu hỏi không có đáp án đúng: 4 đáp án, 0 đúng */
    int q_no_correct = create_test_question(db, top, "Q Khong co dap an dung", "EASY", 4, 0);

    /* 4. Câu hỏi có nhiều hơn 1 đáp án đúng: 4 đáp án, 2 đúng */
    int q_multi_correct = create_test_question(db, top, "Q Nhieu dap an dung", "EASY", 4, 2);

    /* 5. Câu hỏi có topic_id = NULL (chưa phân loại) */
    int q_unassigned = create_test_question(db, 0, "Q Chua phan loai", "EASY", 4, 1);

    /* Gọi selector chọn 10 câu */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 10, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "14a. Selector lọc câu hỏi hợp lệ thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *sel = cJSON_GetObjectItem(root, "selected");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(sel && sel->valueint == 1, "14b. Chỉ có DUY NHẤT 1 câu hợp lệ được chọn");
            if (arr && cJSON_GetArraySize(arr) == 1) {
                cJSON *item = cJSON_GetArrayItem(arr, 0);
                cJSON *qid = cJSON_GetObjectItem(item, "id");
                TEST_ASSERT(qid && qid->valueint == q_valid, "14c. Đúng câu q_valid được chọn");
                TEST_ASSERT(qid && qid->valueint != q_short_ans, "14d. Đã loại trừ câu thiếu đáp án");
                TEST_ASSERT(qid && qid->valueint != q_no_correct, "14e. Đã loại trừ câu 0 đáp án đúng");
                TEST_ASSERT(qid && qid->valueint != q_multi_correct, "15a. Đã loại trừ câu >1 đáp án đúng");
                TEST_ASSERT(qid && qid->valueint != q_unassigned, "15b. Đã loại trừ câu topic_id NULL");
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * TEST 16, 17, 18, 19: TÍNH DUY NHẤT, KHÔNG RÒ RỈ ĐÁP ÁN, TÍNH NGẪU NHIÊN & FK
 * ============================================================================
 */
static void test_uniqueness_security_randomness(void)
{
    printf("\n=== KIỂM THỬ 16-19: Không trùng ID, Bảo mật, Randomness & FK ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0;
    classification_subject_create(db, "S1", "Mon 1", NULL, &sub);
    classification_chapter_create(db, sub, 1, "C1", NULL, &chap);
    classification_lesson_create(db, chap, 1, "L1", NULL, &les);

    int tids[3];
    for (int i = 0; i < 3; i++) {
        char name[64];
        snprintf(name, sizeof(name), "Topic %d", i + 1);
        classification_topic_create(db, les, name, NULL, &tids[i]);
        for (int q = 1; q <= 15; q++) {
            char qbuf[64];
            snprintf(qbuf, sizeof(qbuf), "T%d Q%d", i + 1, q);
            create_test_question(db, tids[i], qbuf, "MEDIUM", 4, 1);
        }
    }

    /* 1. Kiểm tra tính DUY NHẤT (không trùng lặp question_id) khi chọn N = 20 câu */
    char *json = NULL;
    int rc = question_selector_balanced(db, sub, chap, les, NULL, 20, &json);
    TEST_ASSERT(rc == QUESTION_SELECTOR_SUCCESS && json != NULL, "16a. Chọn 20 câu thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            int sz = arr ? cJSON_GetArraySize(arr) : 0;
            TEST_ASSERT(sz == 20, "16b. Trả về đúng 20 câu hỏi");

            int ids[20];
            int has_dup = 0;
            int leak_ans = 0;
            for (int i = 0; i < sz; i++) {
                cJSON *item = cJSON_GetArrayItem(arr, i);
                ids[i] = cJSON_GetObjectItem(item, "id")->valueint;
                if (cJSON_GetObjectItem(item, "is_correct") != NULL ||
                    cJSON_GetObjectItem(item, "answers") != NULL) {
                    leak_ans = 1;
                }
                for (int j = 0; j < i; j++) {
                    if (ids[j] == ids[i]) {
                        has_dup = 1;
                    }
                }
            }
            TEST_ASSERT(has_dup == 0, "16c. TUYỆT ĐỐI KHÔNG có question_id bị trùng lặp");
            TEST_ASSERT(leak_ans == 0, "17a. TUYỆT ĐỐI KHÔNG rò rỉ trường is_correct hoặc answers");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Randomness qua nhiều lần chạy */
    int first_q_ids[10];
    int variations_found = 0;
    for (int run = 0; run < 5; run++) {
        char *run_json = NULL;
        question_selector_balanced(db, sub, chap, les, NULL, 10, &run_json);
        if (run_json) {
            cJSON *root = cJSON_Parse(run_json);
            if (root) {
                cJSON *arr = cJSON_GetObjectItem(root, "questions");
                if (run == 0) {
                    for (int i = 0; i < 10; i++) {
                        first_q_ids[i] = cJSON_GetObjectItem(cJSON_GetArrayItem(arr, i), "id")->valueint;
                    }
                } else {
                    int diff_in_run = 0;
                    for (int i = 0; i < 10; i++) {
                        int cur_id = cJSON_GetObjectItem(cJSON_GetArrayItem(arr, i), "id")->valueint;
                        if (cur_id != first_q_ids[i]) {
                            diff_in_run = 1;
                            break;
                        }
                    }
                    if (diff_in_run) {
                        variations_found++;
                    }
                }
                cJSON_Delete(root);
            }
            free(run_json);
        }
    }
    TEST_ASSERT(variations_found > 0, "18a. Danh sách câu hỏi ngẫu nhiên hóa qua các lần chạy khác nhau");

    /* 3. Kiểm tra PRAGMA foreign_key_check */
    TEST_ASSERT(count_fk_violations(db) == 0, "19a. PRAGMA foreign_key_check = 0 vi phạm");

    sqlite3_close(db);
}

int main(void)
{
    printf("=================================================================\n");
    printf("  BỘ KIỂM THỬ: BALANCED QUESTION SELECTOR (TASK 06.6)\n");
    printf("=================================================================\n");

    test_one_and_equal_topics();
    test_imbalanced_pools_and_redistribution();
    test_n_vs_k_scenarios();
    test_shortage_and_empty_scenarios();
    test_hierarchical_and_difficulty_filters();
    test_invalid_questions_exclusion();
    test_uniqueness_security_randomness();

    printf("\n=================================================================\n");
    printf("  KẾT QUẢ KIỂM THỬ: %d / %d PASS\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");

    return (g_tests_passed == g_tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}

