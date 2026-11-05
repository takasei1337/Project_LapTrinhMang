#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "classification.h"
#include "question_classification.h"

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

/* Helper chèn câu hỏi */
static int insert_question(sqlite3 *db, const char *content, const char *topic, const char *diff, int *out_qid)
{
    const char *sql = "INSERT INTO questions (content, topic, difficulty) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_text(stmt, 1, content, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, topic, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, diff, -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return -1;
    if (out_qid) *out_qid = (int)sqlite3_last_insert_rowid(db);
    return 0;
}

/* Helper chèn đáp án */
static int insert_answer(sqlite3 *db, int qid, const char *content, int is_correct)
{
    const char *sql = "INSERT INTO answers (question_id, content, is_correct) VALUES (?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, qid);
    sqlite3_bind_text(stmt, 2, content, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, is_correct);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? 0 : -1;
}

/*
 * ============================================================================
 * NHÓM 1: GÁN (ASSIGN) VÀ HỦY GÁN (UNASSIGN) QUESTION VÀO TOPIC
 * ============================================================================
 */
static void test_group1_assign_and_unassign(void)
{
    printf("\n=== NHÓM 1: Assign và Unassign Question vào Topic ===\n");
    sqlite3 *db = create_in_memory_db();
    TEST_ASSERT(db != NULL, "1a. Khởi tạo DB in-memory thành công");

    /* Tạo cây phân cấp mẫu: Subject -> Chapter -> Lesson -> Topic A & Topic B */
    int sub_id = 0, chap_id = 0, les_id = 0, top_a = 0, top_b = 0;
    classification_subject_create(db, "NET101", "Mang may tinh", "Mon hoc mang", &sub_id);
    classification_chapter_create(db, sub_id, 1, "Tang Giao van", "Transport Layer", &chap_id);
    classification_lesson_create(db, chap_id, 1, "Giao thuc TCP", "TCP Protocol", &les_id);
    classification_topic_create(db, les_id, "Bat tay 3 buoc (Three-way handshake)", NULL, &top_a);
    classification_topic_create(db, les_id, "Kiem soat luu luong (Flow control)", NULL, &top_b);

    /* Tạo câu hỏi mẫu ban đầu chưa phân loại (topic_id = NULL) */
    int q_id = 0;
    insert_question(db, "Cờ SYN được sử dụng trong bước nào?", "Mang cu", "MEDIUM", &q_id);
    TEST_ASSERT(q_id > 0, "1b. Chèn câu hỏi mẫu thành công");

    /* 1. Gán Question vào Topic A hợp lệ */
    int rc = question_classification_assign(db, q_id, top_a);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS, "1c. Assign Question vào Topic A thành công");

    /* Kiểm tra DB */
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT topic, topic_id FROM questions WHERE id = ?;", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, q_id);
    int top_val = 0;
    char orig_top[256] = {0};
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(orig_top, sizeof(orig_top), "%s", sqlite3_column_text(stmt, 0));
        top_val = sqlite3_column_int(stmt, 1);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(top_val == top_a, "1d. questions.topic_id chính xác bằng top_a");
    TEST_ASSERT(strcmp(orig_top, "Mang cu") == 0, "1e. questions.topic cũ ('Mang cu') bảo toàn 100%");

    /* 2. Chuyển Question từ Topic A sang Topic B */
    rc = question_classification_assign(db, q_id, top_b);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS, "1f. Chuyển Question từ Topic A sang Topic B thành công");

    sqlite3_prepare_v2(db, "SELECT topic, topic_id FROM questions WHERE id = ?;", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, q_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        top_val = sqlite3_column_int(stmt, 1);
        snprintf(orig_top, sizeof(orig_top), "%s", sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(top_val == top_b, "1g. questions.topic_id đã được cập nhật sang top_b");
    TEST_ASSERT(strcmp(orig_top, "Mang cu") == 0, "1h. questions.topic cũ vẫn giữ nguyên vẹn sau khi chuyển");

    /* 3. Unassign Question khỏi Topic */
    rc = question_classification_unassign(db, q_id);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS, "1i. Unassign Question thành công");

    sqlite3_prepare_v2(db, "SELECT topic, topic_id FROM questions WHERE id = ?;", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, q_id);
    int is_null = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        snprintf(orig_top, sizeof(orig_top), "%s", sqlite3_column_text(stmt, 0));
        is_null = (sqlite3_column_type(stmt, 1) == SQLITE_NULL);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(is_null == 1, "1j. questions.topic_id đã trở về NULL sau unassign");
    TEST_ASSERT(strcmp(orig_top, "Mang cu") == 0, "1k. questions.topic cũ tiếp tục được bảo toàn sau unassign");

    /* 4. Assign với Question hoặc Topic không tồn tại */
    rc = question_classification_assign(db, 9999, top_a);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_ERR_NOT_FOUND, "1l. Assign question_id=9999 không tồn tại -> NOT_FOUND");

    rc = question_classification_assign(db, q_id, 9999);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_ERR_NOT_FOUND, "1m. Assign topic_id=9999 không tồn tại -> NOT_FOUND");

    rc = question_classification_unassign(db, 9999);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_ERR_NOT_FOUND, "1n. Unassign question_id=9999 không tồn tại -> NOT_FOUND");

    /* 5. Validation tham số */
    TEST_ASSERT(question_classification_assign(NULL, q_id, top_a) == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "1o. db NULL -> INVALID_PARAM");
    TEST_ASSERT(question_classification_assign(db, 0, top_a) == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "1p. qid <= 0 -> INVALID_PARAM");
    TEST_ASSERT(question_classification_assign(db, q_id, 0) == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "1q. tid <= 0 -> INVALID_PARAM");
    TEST_ASSERT(question_classification_unassign(NULL, q_id) == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "1r. unassign db NULL -> INVALID_PARAM");
    TEST_ASSERT(question_classification_unassign(db, -1) == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "1s. unassign qid < 0 -> INVALID_PARAM");

    TEST_ASSERT(count_fk_violations(db) == 0, "1t. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 2: HAI TOPIC CÙNG TÊN THUỘC HAI LESSON KHÁC NHAU (KHÔNG NHẦM LẪN)
 * ============================================================================
 */
static void test_group2_same_topic_name_different_lessons(void)
{
    printf("\n=== NHÓM 2: Hai Topic cùng tên thuộc hai Lesson khác nhau ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les1 = 0, les2 = 0;
    classification_subject_create(db, "ENG101", "Tieng Anh Chuyen nganh", NULL, &sub);
    classification_chapter_create(db, sub, 1, "Ngu phap", NULL, &chap);
    classification_lesson_create(db, chap, 1, "Thi hien tai", NULL, &les1);
    classification_lesson_create(db, chap, 2, "Thi qua khu", NULL, &les2);

    /* Cả hai bài học đều có topic tên là "Tong quan & Cach dung" */
    int top1 = 0, top2 = 0;
    int rc = classification_topic_create(db, les1, "Tong quan & Cach dung", "Cho thi hien tai", &top1);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && top1 > 0, "2a. Tạo topic 1 ở lesson 1 thành công");

    rc = classification_topic_create(db, les2, "Tong quan & Cach dung", "Cho thi qua khu", &top2);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && top2 > 0, "2b. Tạo topic 2 cùng tên ở lesson 2 thành công");
    TEST_ASSERT(top1 != top2, "2c. Hai topic có ID riêng biệt");

    int q1 = 0, q2 = 0;
    insert_question(db, "Hien tai don dung khi nao?", "Grammar", "EASY", &q1);
    insert_question(db, "Qua khu don dung khi nao?", "Grammar", "MEDIUM", &q2);

    /* Gán tường minh theo topic_id */
    question_classification_assign(db, q1, top1);
    question_classification_assign(db, q2, top2);

    /* Kiểm tra Get Question 1 */
    char *json1 = NULL;
    rc = question_classification_get(db, q1, &json1);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json1 != NULL, "2d. Get q1 thành công");
    if (json1) {
        cJSON *root = cJSON_Parse(json1);
        if (root) {
            cJSON *tid = cJSON_GetObjectItem(root, "topic_id");
            cJSON *lid = cJSON_GetObjectItem(root, "lesson_id");
            cJSON *lname = cJSON_GetObjectItem(root, "lesson_name");
            TEST_ASSERT(tid && tid->valueint == top1, "2e. q1 gắn đúng topic 1");
            TEST_ASSERT(lid && lid->valueint == les1, "2f. q1 gắn đúng lesson 1 ('Thi hien tai')");
            TEST_ASSERT(lname && strcmp(lname->valuestring, "Thi hien tai") == 0, "2g. lesson_name khớp");
            cJSON_Delete(root);
        }
        free(json1);
    }

    /* Kiểm tra Get Question 2 */
    char *json2 = NULL;
    rc = question_classification_get(db, q2, &json2);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json2 != NULL, "2h. Get q2 thành công");
    if (json2) {
        cJSON *root = cJSON_Parse(json2);
        if (root) {
            cJSON *tid = cJSON_GetObjectItem(root, "topic_id");
            cJSON *lid = cJSON_GetObjectItem(root, "lesson_id");
            cJSON *lname = cJSON_GetObjectItem(root, "lesson_name");
            TEST_ASSERT(tid && tid->valueint == top2, "2i. q2 gắn đúng topic 2");
            TEST_ASSERT(lid && lid->valueint == les2, "2j. q2 gắn đúng lesson 2 ('Thi qua khu')");
            TEST_ASSERT(lname && strcmp(lname->valuestring, "Thi qua khu") == 0, "2k. lesson_name khớp");
            cJSON_Delete(root);
        }
        free(json2);
    }

    TEST_ASSERT(count_fk_violations(db) == 0, "2l. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 3: GET QUESTION CLASSIFICATION (ĐÃ PHÂN LOẠI & CHƯA PHÂN LOẠI)
 * ============================================================================
 */
static void test_group3_get_classification(void)
{
    printf("\n=== NHÓM 3: Get Question Classification ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top = 0;
    classification_subject_create(db, "DSA", "Cau truc du lieu va Giai thuat", "Mon DSA", &sub);
    classification_chapter_create(db, sub, 1, "Do thi (Graph)", "Graph Chapter", &chap);
    classification_lesson_create(db, chap, 1, "Duyet do thi", "Graph Traversal", &les);
    classification_topic_create(db, les, "BFS & DFS", "Breadth & Depth First Search", &top);

    int q_assigned = 0, q_unassigned = 0;
    insert_question(db, "Do phuc tap BFS tren do thi la gi?", "DSA cu", "HARD", &q_assigned);
    insert_question(db, "Cay nhi phan tim kiem la gi?", "Trees", "MEDIUM", &q_unassigned);

    question_classification_assign(db, q_assigned, top);

    /* 1. Get câu hỏi đã phân loại */
    char *json_a = NULL;
    int rc = question_classification_get(db, q_assigned, &json_a);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json_a != NULL, "3a. Get câu hỏi đã phân loại thành công");
    if (json_a) {
        cJSON *root = cJSON_Parse(json_a);
        TEST_ASSERT(root != NULL, "3b. JSON parse hợp lệ");
        if (root) {
            cJSON *qid = cJSON_GetObjectItem(root, "question_id");
            cJSON *content = cJSON_GetObjectItem(root, "content");
            cJSON *topic = cJSON_GetObjectItem(root, "topic");
            cJSON *diff = cJSON_GetObjectItem(root, "difficulty");
            cJSON *tid = cJSON_GetObjectItem(root, "topic_id");
            cJSON *tname = cJSON_GetObjectItem(root, "topic_name");
            cJSON *lid = cJSON_GetObjectItem(root, "lesson_id");
            cJSON *cid = cJSON_GetObjectItem(root, "chapter_id");
            cJSON *sid = cJSON_GetObjectItem(root, "subject_id");
            cJSON *scode = cJSON_GetObjectItem(root, "subject_code");
            cJSON *cls = cJSON_GetObjectItem(root, "classification");

            TEST_ASSERT(qid && qid->valueint == q_assigned, "3c. question_id khớp");
            TEST_ASSERT(content && strstr(content->valuestring, "BFS") != NULL, "3d. content khớp");
            TEST_ASSERT(topic && strcmp(topic->valuestring, "DSA cu") == 0, "3e. topic legacy giữ nguyên");
            TEST_ASSERT(diff && strcmp(diff->valuestring, "HARD") == 0, "3f. difficulty HARD");
            TEST_ASSERT(tid && tid->valueint == top, "3g. topic_id khớp");
            TEST_ASSERT(tname && strcmp(tname->valuestring, "BFS & DFS") == 0, "3h. topic_name khớp");
            TEST_ASSERT(lid && lid->valueint == les, "3i. lesson_id khớp");
            TEST_ASSERT(cid && cid->valueint == chap, "3j. chapter_id khớp");
            TEST_ASSERT(sid && sid->valueint == sub, "3k. subject_id khớp");
            TEST_ASSERT(scode && strcmp(scode->valuestring, "DSA") == 0, "3l. subject_code khớp");
            TEST_ASSERT(cls && cJSON_IsObject(cls), "3m. Object nested classification hợp lệ");

            cJSON_Delete(root);
        }
        free(json_a);
    }

    /* 2. Get câu hỏi chưa phân loại */
    char *json_u = NULL;
    rc = question_classification_get(db, q_unassigned, &json_u);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json_u != NULL, "3n. Get câu hỏi chưa phân loại thành công");
    if (json_u) {
        cJSON *root = cJSON_Parse(json_u);
        TEST_ASSERT(root != NULL, "3o. JSON câu hỏi chưa phân loại parse hợp lệ");
        if (root) {
            cJSON *tid = cJSON_GetObjectItem(root, "topic_id");
            cJSON *tname = cJSON_GetObjectItem(root, "topic_name");
            cJSON *lid = cJSON_GetObjectItem(root, "lesson_id");
            cJSON *cls = cJSON_GetObjectItem(root, "classification");

            TEST_ASSERT(tid && cJSON_IsNull(tid), "3p. topic_id thể hiện rõ là null");
            TEST_ASSERT(tname && cJSON_IsNull(tname), "3q. topic_name là null");
            TEST_ASSERT(lid && cJSON_IsNull(lid), "3r. lesson_id là null");
            TEST_ASSERT(cls && cJSON_IsNull(cls), "3s. classification là null");

            cJSON_Delete(root);
        }
        free(json_u);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 4: LỌC DANH SÁCH (SUBJECT, CHAPTER, LESSON, TOPIC, DIFFICULTY, MÂU THUẪN)
 * ============================================================================
 */
static void test_group4_filtering_and_pagination(void)
{
    printf("\n=== NHÓM 4: Lọc Danh sách qua Cây phân cấp & Phân trang ===\n");
    sqlite3 *db = create_in_memory_db();

    /* Thiết lập dữ liệu phân cấp */
    int sub1 = 0, sub2 = 0;
    classification_subject_create(db, "S1", "Subject 1", NULL, &sub1);
    classification_subject_create(db, "S2", "Subject 2", NULL, &sub2);

    int c1 = 0, c2 = 0;
    classification_chapter_create(db, sub1, 1, "S1 Chap 1", NULL, &c1);
    classification_chapter_create(db, sub2, 1, "S2 Chap 1", NULL, &c2);

    int l1 = 0, l2 = 0;
    classification_lesson_create(db, c1, 1, "S1 C1 Les 1", NULL, &l1);
    classification_lesson_create(db, c2, 1, "S2 C2 Les 1", NULL, &l2);

    int t1 = 0, t2 = 0;
    classification_topic_create(db, l1, "Topic 1 (S1)", NULL, &t1);
    classification_topic_create(db, l2, "Topic 2 (S2)", NULL, &t2);

    /* Tạo các câu hỏi:
     * q1: Topic 1 (Sub 1), EASY
     * q2: Topic 1 (Sub 1), MEDIUM
     * q3: Topic 1 (Sub 1), HARD
     * q4: Topic 2 (Sub 2), EASY
     * q5: Topic 2 (Sub 2), HARD
     */
    int q1 = 0, q2 = 0, q3 = 0, q4 = 0, q5 = 0;
    insert_question(db, "Q1 Easy", "Top", "EASY", &q1);
    insert_question(db, "Q2 Med", "Top", "MEDIUM", &q2);
    insert_question(db, "Q3 Hard", "Top", "HARD", &q3);
    insert_question(db, "Q4 Easy", "Top", "EASY", &q4);
    insert_question(db, "Q5 Hard", "Top", "HARD", &q5);

    /* Chèn đáp án cho q1 để kiểm tra bảo mật không rò rỉ đáp án đúng */
    insert_answer(db, q1, "Dap an dung cho Q1", 1);
    insert_answer(db, q1, "Dap an sai cho Q1", 0);

    question_classification_assign(db, q1, t1);
    question_classification_assign(db, q2, t1);
    question_classification_assign(db, q3, t1);
    question_classification_assign(db, q4, t2);
    question_classification_assign(db, q5, t2);

    /* 1. Lọc theo Subject 1 */
    char *json = NULL;
    int rc = question_classification_list(db, sub1, 0, 0, 0, NULL, 10, 0, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "4a. List lọc theo subject 1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            cJSON *cnt = cJSON_GetObjectItem(root, "count");
            TEST_ASSERT(tot && tot->valueint == 3, "4b. Subject 1 có đúng 3 câu hỏi (q1, q2, q3)");
            TEST_ASSERT(cnt && cnt->valueint == 3, "4c. count = 3");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Lọc theo Subject 2 */
    json = NULL;
    rc = question_classification_list(db, sub2, 0, 0, 0, NULL, 10, 0, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(tot && tot->valueint == 2, "4d. Subject 2 có đúng 2 câu hỏi (q4, q5)");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 3. Lọc kết hợp: Subject 1 + Chapter 1 + EASY */
    json = NULL;
    rc = question_classification_list(db, sub1, c1, 0, 0, "EASY", 10, 0, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "4e. Lọc kết hợp Sub1 + Chap1 + EASY thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(tot && tot->valueint == 1, "4f. Chỉ có 1 câu khớp (q1)");
            if (arr && cJSON_GetArraySize(arr) == 1) {
                cJSON *item = cJSON_GetArrayItem(arr, 0);
                cJSON *qid = cJSON_GetObjectItem(item, "id");
                cJSON *ans_cnt = cJSON_GetObjectItem(item, "answer_count");
                /* Kiểm tra an toàn: KHÔNG có trường is_correct */
                cJSON *is_corr = cJSON_GetObjectItem(item, "is_correct");
                TEST_ASSERT(qid && qid->valueint == q1, "4g. Câu hỏi trả về là q1");
                TEST_ASSERT(ans_cnt && ans_cnt->valueint == 2, "4h. answer_count = 2");
                TEST_ASSERT(is_corr == NULL, "4i. KHÔNG rò rỉ trường is_correct trong danh sách");
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 4. Bộ lọc mâu thuẫn: Subject 1 nhưng Chapter lại thuộc Subject 2 (c2) */
    json = NULL;
    rc = question_classification_list(db, sub1, c2, 0, 0, NULL, 10, 0, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "4j. Lọc mâu thuẫn (Sub1 + Chap của Sub2) thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            cJSON *cnt = cJSON_GetObjectItem(root, "count");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(tot && tot->valueint == 0, "4k. Mâu thuẫn -> total = 0");
            TEST_ASSERT(cnt && cnt->valueint == 0, "4l. Mâu thuẫn -> count = 0");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 0, "4m. Mâu thuẫn -> mảng rỗng []");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 5. Phân trang: limit = 2, offset = 1 trên Subject 1 */
    json = NULL;
    rc = question_classification_list(db, sub1, 0, 0, 0, NULL, 2, 1, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "4n. Phân trang limit=2, offset=1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            cJSON *cnt = cJSON_GetObjectItem(root, "count");
            cJSON *lim = cJSON_GetObjectItem(root, "limit");
            cJSON *off = cJSON_GetObjectItem(root, "offset");
            TEST_ASSERT(tot && tot->valueint == 3, "4o. Total = 3");
            TEST_ASSERT(cnt && cnt->valueint == 2, "4p. Count = 2");
            TEST_ASSERT(lim && lim->valueint == 2, "4q. Limit = 2");
            TEST_ASSERT(off && off->valueint == 1, "4r. Offset = 1");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 6. Độ khó không hợp lệ -> INVALID_PARAM */
    json = NULL;
    rc = question_classification_list(db, 0, 0, 0, 0, "EXTREME_HARD", 10, 0, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_ERR_INVALID_PARAM, "4s. Difficulty không hợp lệ -> INVALID_PARAM");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 5: DANH SÁCH CÂU HỎI CHƯA PHÂN LOẠI (UNASSIGNED)
 * ============================================================================
 */
static void test_group5_list_unassigned(void)
{
    printf("\n=== NHÓM 5: Danh sách Câu hỏi Chưa Phân Loại (Unassigned) ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top = 0;
    classification_subject_create(db, "SUB", "Sub", NULL, &sub);
    classification_chapter_create(db, sub, 1, "Chap", NULL, &chap);
    classification_lesson_create(db, chap, 1, "Les", NULL, &les);
    classification_topic_create(db, les, "Top", NULL, &top);

    int q1 = 0, q2 = 0, q3 = 0;
    insert_question(db, "Q_Unassigned_1", "OldTopic", "EASY", &q1);
    insert_question(db, "Q_Unassigned_2", "OldTopic", "MEDIUM", &q2);
    insert_question(db, "Q_Assigned_3", "OldTopic", "HARD", &q3);

    /* Gán q3 vào topic */
    question_classification_assign(db, q3, top);

    /* Lấy danh sách câu hỏi chưa phân loại */
    char *json = NULL;
    int rc = question_classification_list_unassigned(db, 10, 0, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "5a. question_classification_list_unassigned() thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        TEST_ASSERT(root != NULL, "5b. JSON unassigned parse hợp lệ");
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            cJSON *cnt = cJSON_GetObjectItem(root, "count");
            cJSON *arr = cJSON_GetObjectItem(root, "questions");
            TEST_ASSERT(tot && tot->valueint == 2, "5c. Đúng 2 câu hỏi chưa phân loại (q1, q2)");
            TEST_ASSERT(cnt && cnt->valueint == 2, "5d. Count = 2");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 2, "5e. Trả về đúng 2 phần tử");

            if (arr && cJSON_GetArraySize(arr) >= 2) {
                cJSON *item1 = cJSON_GetArrayItem(arr, 0);
                cJSON *tid1 = cJSON_GetObjectItem(item1, "topic_id");
                TEST_ASSERT(tid1 && cJSON_IsNull(tid1), "5f. Phần tử trong danh sách unassigned có topic_id = null");
            }
            cJSON_Delete(root);
        }
        free(json);
    }

    /* Sau khi gán nốt q1, danh sách unassigned chỉ còn 1 câu */
    question_classification_assign(db, q1, top);

    json = NULL;
    rc = question_classification_list_unassigned(db, 10, 0, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *tot = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(tot && tot->valueint == 1, "5g. Sau khi gán q1, unassigned còn 1 câu (q2)");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 6: AN TOÀN SQL INJECTION & BẢO VỆ FOREIGN KEYS
 * ============================================================================
 */
static void test_group6_sql_injection_and_fk_safety(void)
{
    printf("\n=== NHÓM 6: An toàn SQL Injection & Foreign Key Safety ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub = 0, chap = 0, les = 0, top = 0;
    classification_subject_create(db, "SEC", "Bao mat Thong tin", NULL, &sub);
    classification_chapter_create(db, sub, 1, "Web Security", NULL, &chap);
    classification_lesson_create(db, chap, 1, "SQL Injection", NULL, &les);
    classification_topic_create(db, les, "Prepared Statements", NULL, &top);

    /* Thử chèn nội dung có nháy đơn, dấu chấm phẩy, ký tự SQL injection */
    int q_inj = 0;
    const char *inj_content = "SELECT * FROM users WHERE '1'='1'; DROP TABLE questions; --";
    const char *inj_topic = "Hack' OR 1=1; --";
    insert_question(db, inj_content, inj_topic, "HARD", &q_inj);
    TEST_ASSERT(q_inj > 0, "6a. Chèn câu hỏi chứa ký tự SQL đặc biệt thành công");

    /* Gán vào topic qua prepared statements */
    int rc = question_classification_assign(db, q_inj, top);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS, "6b. Assign an toàn qua prepared statement");

    /* Đọc lại và kiểm tra nội dung nguyên vẹn */
    char *json = NULL;
    rc = question_classification_get(db, q_inj, &json);
    TEST_ASSERT(rc == QUESTION_CLASSIFICATION_SUCCESS && json != NULL, "6c. Get câu hỏi chứa SQL injection thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *cnt = cJSON_GetObjectItem(root, "content");
            cJSON *top_str = cJSON_GetObjectItem(root, "topic");
            TEST_ASSERT(cnt && strcmp(cnt->valuestring, inj_content) == 0, "6d. Nội dung content bảo toàn 100%");
            TEST_ASSERT(top_str && strcmp(top_str->valuestring, inj_topic) == 0, "6e. Chuỗi topic bảo toàn 100%");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* Kiểm tra bảng questions vẫn nguyên vẹn */
    sqlite3_stmt *stmt = NULL;
    rc = sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM questions;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "6f. Bảng questions không bị DROP hay hỏng hóc");
    int count = 0;
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    TEST_ASSERT(count == 1, "6g. Số lượng bản ghi câu hỏi = 1");

    /* Kiểm tra PRAGMA foreign_key_check = 0 vi phạm */
    TEST_ASSERT(count_fk_violations(db) == 0, "6h. PRAGMA foreign_key_check = 0 vi phạm");

    sqlite3_close(db);
}

int main(void)
{
    printf("=================================================================\n");
    printf("  BỘ KIỂM THỬ: QUESTION CLASSIFICATION MAPPING (TASK 06.5)\n");
    printf("=================================================================\n");

    test_group1_assign_and_unassign();
    test_group2_same_topic_name_different_lessons();
    test_group3_get_classification();
    test_group4_filtering_and_pagination();
    test_group5_list_unassigned();
    test_group6_sql_injection_and_fk_safety();

    printf("\n=================================================================\n");
    printf("  KẾT QUẢ KIỂM THỬ: %d / %d PASS\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");

    return (g_tests_passed == g_tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}

