#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "practice.h"
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
    if (rc != SQLITE_OK || !stmt) return -1;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

static int count_rows_where(sqlite3 *db, const char *table, const char *where)
{
    char sql[512];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s WHERE %s;", table, where);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return -1;
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

static int add_test_question(sqlite3 *db, const char *content, int topic_id, const char *difficulty,
                             const char *ans1, int corr1, const char *ans2, int corr2)
{
    char sql[512];
    if (topic_id > 0) {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO questions (content, topic, topic_id, difficulty) "
                 "VALUES ('%s', 'TopicText', %d, '%s');",
                 content, topic_id, difficulty);
    } else {
        snprintf(sql, sizeof(sql),
                 "INSERT INTO questions (content, topic, topic_id, difficulty) "
                 "VALUES ('%s', 'TopicText', NULL, '%s');",
                 content, difficulty);
    }
    exec_sql(db, sql);
    int q_id = (int)sqlite3_last_insert_rowid(db);

    snprintf(sql, sizeof(sql),
             "INSERT INTO answers (question_id, content, is_correct) VALUES (%d, '%s', %d);",
             q_id, ans1, corr1);
    exec_sql(db, sql);

    snprintf(sql, sizeof(sql),
             "INSERT INTO answers (question_id, content, is_correct) VALUES (%d, '%s', %d);",
             q_id, ans2, corr2);
    exec_sql(db, sql);

    return q_id;
}

static sqlite3 *setup_test_db(void)
{
    sqlite3 *db = NULL;
    int rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open memory DB\n");
        exit(EXIT_FAILURE);
    }

    exec_sql(db, "PRAGMA foreign_keys = ON;");

    char *schema_sql = read_file_to_string("database/schema.sql");
    if (!schema_sql) {
        fprintf(stderr, "Failed to read database/schema.sql\n");
        exit(EXIT_FAILURE);
    }
    exec_sql(db, schema_sql);
    free(schema_sql);

    /* Setup test users */
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (1, 'student1', 'hash1', 'USER');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) "
                 "VALUES (2, 'student2', 'hash2', 'USER');");

    /* Hierarchy: Subject 1 -> Chapter 1 -> Lesson 1 -> Topics 1, 2, 3 */
    exec_sql(db, "INSERT INTO subjects (id, code, name, description) "
                 "VALUES (1, 'MMT', 'Mang may tinh', 'Mon hoc mang');");
    exec_sql(db, "INSERT INTO subjects (id, code, name, description) "
                 "VALUES (2, 'HDH', 'He dieu hanh', 'Mon hoc HDH');");

    exec_sql(db, "INSERT INTO chapters (id, subject_id, name, order_index) "
                 "VALUES (1, 1, 'Chuong 1: Tong quan', 1);");
    exec_sql(db, "INSERT INTO chapters (id, subject_id, name, order_index) "
                 "VALUES (2, 1, 'Chuong 2: Tang mang', 2);");

    exec_sql(db, "INSERT INTO lessons (id, chapter_id, name, order_index) "
                 "VALUES (1, 1, 'Bai 1: Mo hinh OSI', 1);");
    exec_sql(db, "INSERT INTO lessons (id, chapter_id, name, order_index) "
                 "VALUES (2, 1, 'Bai 2: Giao thuc TCP/IP', 2);");

    exec_sql(db, "INSERT INTO topics (id, lesson_id, name, description) "
                 "VALUES (1, 1, 'Tang ung dung', 'Ung dung');");
    exec_sql(db, "INSERT INTO topics (id, lesson_id, name, description) "
                 "VALUES (2, 1, 'Tang van chuyen', 'Van chuyen');");
    exec_sql(db, "INSERT INTO topics (id, lesson_id, name, description) "
                 "VALUES (3, 1, 'Tang mang', 'Mang');");

    /* Populate Questions:
     * Topic 1: 5 questions (3 EASY, 2 MEDIUM)
     * Topic 2: 5 questions (3 EASY, 2 MEDIUM)
     * Topic 3: 5 questions (3 EASY, 2 MEDIUM)
     */
    for (int i = 1; i <= 3; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 1 Easy Q%d", i);
        add_test_question(db, buf, 1, "EASY", "A", 1, "B", 0);
    }
    for (int i = 1; i <= 2; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 1 Med Q%d", i);
        add_test_question(db, buf, 1, "MEDIUM", "A", 1, "B", 0);
    }

    for (int i = 1; i <= 3; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 2 Easy Q%d", i);
        add_test_question(db, buf, 2, "EASY", "A", 1, "B", 0);
    }
    for (int i = 1; i <= 2; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 2 Med Q%d", i);
        add_test_question(db, buf, 2, "MEDIUM", "A", 1, "B", 0);
    }

    for (int i = 1; i <= 3; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 3 Easy Q%d", i);
        add_test_question(db, buf, 3, "EASY", "A", 1, "B", 0);
    }
    for (int i = 1; i <= 2; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Topic 3 Med Q%d", i);
        add_test_question(db, buf, 3, "MEDIUM", "A", 1, "B", 0);
    }

    /* Unassigned question (topic_id is NULL) */
    add_test_question(db, "Unassigned Q1", 0, "EASY", "A", 1, "B", 0);

    return db;
}

/*
 * ============================================================================
 * Test Suites
 * ============================================================================
 */

static void test_practice_start_balanced_success(void)
{
    printf("\n--- Test: practice_start_balanced_success ---\n");
    sqlite3 *db = setup_test_db();

    char *json_out = NULL;
    int attempt_id = practice_start_balanced(db, 1, 1, 1, 1, NULL, 6, &json_out);
    TEST_ASSERT(attempt_id > 0, "practice_start_balanced returns attempt_id > 0");
    TEST_ASSERT(json_out != NULL, "json_out is not NULL");

    /* Validate JSON structure */
    cJSON *root = cJSON_Parse(json_out);
    TEST_ASSERT(root != NULL, "Output JSON parses successfully");
    if (root) {
        cJSON *att_item = cJSON_GetObjectItem(root, "attempt_id");
        TEST_ASSERT(att_item && att_item->valueint == attempt_id, "attempt_id in JSON matches return value");

        cJSON *mode_item = cJSON_GetObjectItem(root, "mode");
        TEST_ASSERT(mode_item && strcmp(mode_item->valuestring, "PRACTICE") == 0, "mode is 'PRACTICE'");

        cJSON *total_item = cJSON_GetObjectItem(root, "total_questions");
        TEST_ASSERT(total_item && total_item->valueint == 6, "total_questions is 6");

        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        TEST_ASSERT(q_arr && cJSON_GetArraySize(q_arr) == 6, "questions array has 6 items");

        /* Validate that answers are present and is_correct is NOT leaked */
        int no_leak = 1;
        int has_answers = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q_obj = cJSON_GetArrayItem(q_arr, i);
            if (cJSON_GetObjectItem(q_obj, "is_correct") != NULL) {
                no_leak = 0;
            }
            cJSON *ans_arr = cJSON_GetObjectItem(q_obj, "answers");
            if (!ans_arr || cJSON_GetArraySize(ans_arr) < 2) {
                has_answers = 0;
            } else {
                for (int j = 0; j < cJSON_GetArraySize(ans_arr); j++) {
                    cJSON *a_obj = cJSON_GetArrayItem(ans_arr, j);
                    if (cJSON_GetObjectItem(a_obj, "is_correct") != NULL) {
                        no_leak = 0;
                    }
                }
            }
        }
        TEST_ASSERT(no_leak, "Answers array does NOT leak 'is_correct'");
        TEST_ASSERT(has_answers, "Each question has valid answers array");
        cJSON_Delete(root);
    }
    free(json_out);

    /* Check DB records */
    int att_count = count_rows_where(db, "exam_attempts", "mode = 'PRACTICE'");
    TEST_ASSERT(att_count == 1, "Exactly 1 attempt record created in exam_attempts");

    int pq_count = count_rows_where(db, "practice_questions", "attempt_id = 1");
    TEST_ASSERT(pq_count == 6, "Exactly 6 records created in practice_questions");

    /* Verify position sequence 1..6 */
    int pos_ok = 1;
    for (int p = 1; p <= 6; p++) {
        char where[128];
        snprintf(where, sizeof(where), "attempt_id = %d AND position = %d", attempt_id, p);
        if (count_rows_where(db, "practice_questions", where) != 1) {
            pos_ok = 0;
            break;
        }
    }
    TEST_ASSERT(pos_ok, "practice_questions positions are strictly consecutive 1..6");

    /* Verify unique question_ids in attempt */
    int distinct_q = count_rows_where(db, "practice_questions",
        "attempt_id = 1 GROUP BY question_id HAVING COUNT(*) > 1");
    TEST_ASSERT(distinct_q == 0, "No duplicate question_ids in practice_questions");

    sqlite3_close(db);
}

static void test_practice_start_balanced_distribution(void)
{
    printf("\n--- Test: practice_start_balanced_distribution ---\n");
    sqlite3 *db = setup_test_db();

    /* Request 6 questions across 3 topics: each topic has 5 questions -> exactly 2 per topic! */
    char *json_out = NULL;
    int attempt_id = practice_start_balanced(db, 1, 1, 1, 1, NULL, 6, &json_out);
    TEST_ASSERT(attempt_id > 0, "Balanced start succeeds for 6 questions");

    cJSON *root = cJSON_Parse(json_out);
    TEST_ASSERT(root != NULL, "JSON parsed");
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        int count_t1 = 0, count_t2 = 0, count_t3 = 0;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q = cJSON_GetArrayItem(q_arr, i);
            int tid = cJSON_GetObjectItem(q, "topic_id")->valueint;
            if (tid == 1) count_t1++;
            else if (tid == 2) count_t2++;
            else if (tid == 3) count_t3++;
        }
        TEST_ASSERT(count_t1 == 2 && count_t2 == 2 && count_t3 == 2,
                    "Distribution is perfectly balanced: 2 from Topic 1, 2 from Topic 2, 2 from Topic 3");
        cJSON_Delete(root);
    }
    free(json_out);

    /* Request 3 questions across 3 topics -> exactly 1 per topic */
    attempt_id = practice_start_balanced(db, 1, 1, 1, 1, NULL, 3, &json_out);
    TEST_ASSERT(attempt_id > 0, "Balanced start succeeds for 3 questions");
    root = cJSON_Parse(json_out);
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        int count_t1 = 0, count_t2 = 0, count_t3 = 0;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q = cJSON_GetArrayItem(q_arr, i);
            int tid = cJSON_GetObjectItem(q, "topic_id")->valueint;
            if (tid == 1) count_t1++;
            else if (tid == 2) count_t2++;
            else if (tid == 3) count_t3++;
        }
        TEST_ASSERT(count_t1 == 1 && count_t2 == 1 && count_t3 == 1,
                    "Distribution of 3 questions: exactly 1 per topic");
        cJSON_Delete(root);
    }
    free(json_out);

    sqlite3_close(db);
}

static void test_practice_start_balanced_filters(void)
{
    printf("\n--- Test: practice_start_balanced_filters ---\n");
    sqlite3 *db = setup_test_db();

    /* Filter by difficulty 'EASY': 3 topics have 3 EASY questions each (total 9) */
    char *json_out = NULL;
    int attempt_id = practice_start_balanced(db, 1, 1, 1, 1, "EASY", 6, &json_out);
    TEST_ASSERT(attempt_id > 0, "Balanced start with EASY filter succeeds");
    cJSON *root = cJSON_Parse(json_out);
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        int all_easy = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q = cJSON_GetArrayItem(q_arr, i);
            const char *diff = cJSON_GetObjectItem(q, "difficulty")->valuestring;
            if (strcmp(diff, "EASY") != 0) {
                all_easy = 0;
            }
        }
        TEST_ASSERT(all_easy, "All selected questions match EASY difficulty");
        cJSON_Delete(root);
    }
    free(json_out);

    /* Filter by extended API practice_start_balanced_ex with topic_id = 2 */
    attempt_id = practice_start_balanced_ex(db, 1, 1, 1, 1, 2, NULL, 3, &json_out);
    TEST_ASSERT(attempt_id > 0, "practice_start_balanced_ex with topic_id = 2 succeeds");
    root = cJSON_Parse(json_out);
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        int all_t2 = 1;
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q = cJSON_GetArrayItem(q_arr, i);
            int tid = cJSON_GetObjectItem(q, "topic_id")->valueint;
            if (tid != 2) all_t2 = 0;
        }
        TEST_ASSERT(all_t2, "All selected questions match topic_id = 2");
        cJSON_Delete(root);
    }
    free(json_out);

    sqlite3_close(db);
}

static void test_practice_start_balanced_insufficient_questions(void)
{
    printf("\n--- Test: practice_start_balanced_insufficient_questions ---\n");
    sqlite3 *db = setup_test_db();

    int att_before = count_rows(db, "exam_attempts");
    int pq_before = count_rows(db, "practice_questions");

    /* Request 40 questions when only 15 available in the classification tree */
    char *json_out = (char *)0x1234;
    int rc = practice_start_balanced(db, 1, 1, 1, 1, NULL, 40, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_INSUFFICIENT_QUESTIONS,
                "Returns PRACTICE_ERR_INSUFFICIENT_QUESTIONS when not enough questions");
    TEST_ASSERT(json_out == NULL, "json_out set to NULL on failure");

    /* Verify no attempt created in exam_attempts */
    int att_after = count_rows(db, "exam_attempts");
    TEST_ASSERT(att_after == att_before, "No attempt created in exam_attempts");

    int pq_after = count_rows(db, "practice_questions");
    TEST_ASSERT(pq_after == pq_before, "No questions inserted into practice_questions");

    /* Autocommit must be active (no uncommitted transaction) */
    TEST_ASSERT(sqlite3_get_autocommit(db) != 0, "Database autocommit is active after rollback");

    sqlite3_close(db);
}

static void test_practice_start_balanced_zero_questions(void)
{
    printf("\n--- Test: practice_start_balanced_zero_questions ---\n");
    sqlite3 *db = setup_test_db();

    int att_before = count_rows(db, "exam_attempts");

    /* Filter by HARD difficulty where 0 questions exist */
    char *json_out = NULL;
    int rc = practice_start_balanced(db, 1, 1, 1, 1, "HARD", 5, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_INSUFFICIENT_QUESTIONS,
                "Returns PRACTICE_ERR_INSUFFICIENT_QUESTIONS when 0 questions match filter");
    TEST_ASSERT(json_out == NULL, "json_out is NULL");

    int att_after = count_rows(db, "exam_attempts");
    TEST_ASSERT(att_after == att_before, "No attempt record created for 0 matches");
    TEST_ASSERT(sqlite3_get_autocommit(db) != 0, "Autocommit is 1");

    /* Non-existent hierarchy ID */
    rc = practice_start_balanced(db, 1, 999, 1, 1, NULL, 5, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND,
                "Returns PRACTICE_ERR_NOT_FOUND for non-existent subject_id");
    TEST_ASSERT(sqlite3_get_autocommit(db) != 0, "Autocommit is 1");

    sqlite3_close(db);
}

static void test_practice_start_balanced_invalid_user(void)
{
    printf("\n--- Test: practice_start_balanced_invalid_user ---\n");
    sqlite3 *db = setup_test_db();

    int att_before = count_rows(db, "exam_attempts");

    char *json_out = NULL;
    int rc = practice_start_balanced(db, 9999, 1, 1, 1, NULL, 5, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "Returns PRACTICE_ERR_NOT_FOUND for non-existent user");
    TEST_ASSERT(json_out == NULL, "json_out is NULL");

    int att_after = count_rows(db, "exam_attempts");
    TEST_ASSERT(att_after == att_before, "No attempt record created");
    TEST_ASSERT(sqlite3_get_autocommit(db) != 0, "Autocommit is 1");

    sqlite3_close(db);
}

static void test_practice_start_balanced_invalid_params(void)
{
    printf("\n--- Test: practice_start_balanced_invalid_params ---\n");
    sqlite3 *db = setup_test_db();

    char *json_out = NULL;
    /* NULL out pointer */
    TEST_ASSERT(practice_start_balanced(db, 1, 1, 1, 1, NULL, 5, NULL) == PRACTICE_ERR_INVALID_PARAM,
                "NULL out_questions_json returns PRACTICE_ERR_INVALID_PARAM");

    /* NULL db */
    TEST_ASSERT(practice_start_balanced(NULL, 1, 1, 1, 1, NULL, 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "NULL db returns PRACTICE_ERR_INVALID_PARAM");

    /* Invalid user_id */
    TEST_ASSERT(practice_start_balanced(db, 0, 1, 1, 1, NULL, 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "user_id = 0 returns PRACTICE_ERR_INVALID_PARAM");
    TEST_ASSERT(practice_start_balanced(db, -1, 1, 1, 1, NULL, 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "user_id = -1 returns PRACTICE_ERR_INVALID_PARAM");

    /* Invalid num_questions */
    TEST_ASSERT(practice_start_balanced(db, 1, 1, 1, 1, NULL, 0, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "num_questions = 0 returns PRACTICE_ERR_INVALID_PARAM");
    TEST_ASSERT(practice_start_balanced(db, 1, 1, 1, 1, NULL, -5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "num_questions = -5 returns PRACTICE_ERR_INVALID_PARAM");
    TEST_ASSERT(practice_start_balanced(db, 1, 1, 1, 1, NULL, 51, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "num_questions = 51 (> MAX 50) returns PRACTICE_ERR_INVALID_PARAM");

    /* Negative hierarchy IDs */
    TEST_ASSERT(practice_start_balanced(db, 1, -1, 1, 1, NULL, 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "Negative subject_id returns PRACTICE_ERR_INVALID_PARAM");
    TEST_ASSERT(practice_start_balanced_ex(db, 1, 1, 1, 1, -2, NULL, 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "Negative topic_id returns PRACTICE_ERR_INVALID_PARAM");

    /* Invalid difficulty string */
    TEST_ASSERT(practice_start_balanced(db, 1, 1, 1, 1, "SUPER_HARD", 5, &json_out) == PRACTICE_ERR_INVALID_PARAM,
                "Invalid difficulty 'SUPER_HARD' returns PRACTICE_ERR_INVALID_PARAM");

    sqlite3_close(db);
}

static void test_practice_start_balanced_security_no_leak(void)
{
    printf("\n--- Test: practice_start_balanced_security_no_leak ---\n");
    sqlite3 *db = setup_test_db();

    char *json_out = NULL;
    int attempt_id = practice_start_balanced(db, 1, 1, 1, 1, NULL, 5, &json_out);
    TEST_ASSERT(attempt_id > 0, "Attempt created");
    TEST_ASSERT(json_out != NULL, "JSON created");

    /* Substring search for is_correct */
    char *found = strstr(json_out, "is_correct");
    TEST_ASSERT(found == NULL, "JSON output string contains absolutely no 'is_correct'");

    free(json_out);
    sqlite3_close(db);
}

static void test_practice_submit_with_balanced_attempt(void)
{
    printf("\n--- Test: practice_submit_with_balanced_attempt ---\n");
    sqlite3 *db = setup_test_db();

    /* 1. Start balanced practice session with 4 questions */
    char *q_json = NULL;
    int attempt_id = practice_start_balanced(db, 1, 1, 1, 1, NULL, 4, &q_json);
    TEST_ASSERT(attempt_id > 0, "Balanced attempt created with ID > 0");
    TEST_ASSERT(q_json != NULL, "q_json created");

    /* 2. Build answers payload: answer 3 correctly and 1 incorrectly */
    cJSON *root = cJSON_Parse(q_json);
    TEST_ASSERT(root != NULL, "Parsed q_json");

    cJSON *sub_arr = cJSON_CreateArray();
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        for (int i = 0; i < cJSON_GetArraySize(q_arr); i++) {
            cJSON *q_node = cJSON_GetArrayItem(q_arr, i);
            int qid = cJSON_GetObjectItem(q_node, "question_id")->valueint;

            /* Query correct and wrong answers from DB to simulate user submission */
            sqlite3_stmt *stmt = NULL;
            const char *sql = "SELECT id, is_correct FROM answers WHERE question_id = ? ORDER BY id ASC;";
            sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, qid);

            int correct_aid = 0;
            int wrong_aid = 0;
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                int aid = sqlite3_column_int(stmt, 0);
                int is_c = sqlite3_column_int(stmt, 1);
                if (is_c == 1) correct_aid = aid;
                else wrong_aid = aid;
            }
            sqlite3_finalize(stmt);

            cJSON *ans_item = cJSON_CreateObject();
            cJSON_AddNumberToObject(ans_item, "question_id", qid);
            if (i < 3) {
                /* Correct answer */
                cJSON_AddNumberToObject(ans_item, "answer_id", correct_aid);
            } else {
                /* Wrong answer for the 4th question */
                cJSON_AddNumberToObject(ans_item, "answer_id", wrong_aid);
            }
            cJSON_AddItemToArray(sub_arr, ans_item);
        }
        cJSON_Delete(root);
    }
    free(q_json);

    char *ans_json_str = cJSON_PrintUnformatted(sub_arr);
    cJSON_Delete(sub_arr);

    /* 3. Submit attempt */
    char *result_json = NULL;
    int sub_rc = practice_submit(db, attempt_id, 1, ans_json_str, &result_json);
    TEST_ASSERT(sub_rc == PRACTICE_SUCCESS, "practice_submit returns PRACTICE_SUCCESS");
    TEST_ASSERT(result_json != NULL, "result_json is not NULL");

    /* 4. Validate grading result: 3 / 4 correct -> score = 7.5 */
    cJSON *res_root = cJSON_Parse(result_json);
    TEST_ASSERT(res_root != NULL, "Result JSON parsed successfully");
    if (res_root) {
        cJSON *score_item = cJSON_GetObjectItem(res_root, "score");
        cJSON *correct_item = cJSON_GetObjectItem(res_root, "correct_count");
        cJSON *total_item = cJSON_GetObjectItem(res_root, "total_questions");

        TEST_ASSERT(correct_item && correct_item->valueint == 3, "correct_count is 3");
        TEST_ASSERT(total_item && total_item->valueint == 4, "total_questions is 4");
        TEST_ASSERT(score_item && (score_item->valuedouble >= 7.49 && score_item->valuedouble <= 7.51),
                    "score is 7.5 on 10.0 scale (10.0 * 3 / 4)");
        cJSON_Delete(res_root);
    }
    free(result_json);

    /* 5. Check exam_attempts record: score == 7.5 and submitted_at IS NOT NULL */
    sqlite3_stmt *stmt = NULL;
    sqlite3_prepare_v2(db, "SELECT score, submitted_at FROM exam_attempts WHERE id = ?;", -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, attempt_id);
    int step_rc = sqlite3_step(stmt);
    TEST_ASSERT(step_rc == SQLITE_ROW, "Attempt row retrieved");
    if (step_rc == SQLITE_ROW) {
        double db_score = sqlite3_column_double(stmt, 0);
        const char *sub_at = (const char *)sqlite3_column_text(stmt, 1);
        TEST_ASSERT(db_score >= 7.49 && db_score <= 7.51, "DB score is 7.5");
        TEST_ASSERT(sub_at != NULL && strlen(sub_at) > 0, "DB submitted_at is recorded");
    }
    sqlite3_finalize(stmt);

    /* 6. Resubmit attempt: must return PRACTICE_ERR_ALREADY_DONE */
    char *resubmit_json = NULL;
    int resub_rc = practice_submit(db, attempt_id, 1, ans_json_str, &resubmit_json);
    TEST_ASSERT(resub_rc == PRACTICE_ERR_ALREADY_DONE, "Re-submission prevented (PRACTICE_ERR_ALREADY_DONE)");
    TEST_ASSERT(resubmit_json == NULL, "Resubmit result_json is NULL");

    free(ans_json_str);
    sqlite3_close(db);
}

static void test_practice_start_traditional_regression(void)
{
    printf("\n--- Test: practice_start_traditional_regression ---\n");
    sqlite3 *db = setup_test_db();

    /* Original practice_start() with topic string and difficulty */
    char *out_json = NULL;
    int attempt_id = practice_start(db, 1, "TopicText", "EASY", 4, &out_json);
    TEST_ASSERT(attempt_id > 0, "Traditional practice_start still works and returns attempt_id > 0");
    TEST_ASSERT(out_json != NULL, "out_json is generated");

    cJSON *root = cJSON_Parse(out_json);
    TEST_ASSERT(root != NULL, "JSON parsed");
    if (root) {
        cJSON *q_arr = cJSON_GetObjectItem(root, "questions");
        TEST_ASSERT(q_arr && cJSON_GetArraySize(q_arr) == 4, "4 questions generated by traditional practice_start");
        cJSON_Delete(root);
    }
    free(out_json);

    sqlite3_close(db);
}

static void test_foreign_key_integrity(void)
{
    printf("\n--- Test: foreign_key_integrity ---\n");
    sqlite3 *db = setup_test_db();

    char *json_out = NULL;
    practice_start_balanced(db, 1, 1, 1, 1, NULL, 6, &json_out);
    free(json_out);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK, "PRAGMA foreign_key_check prepared");

    int fk_violations = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fk_violations++;
    }
    sqlite3_finalize(stmt);

    TEST_ASSERT(fk_violations == 0, "Zero foreign key violations in database");

    sqlite3_close(db);
}

static void test_caller_transaction_handling(void)
{
    printf("\n--- Test: caller_transaction_handling ---\n");
    sqlite3 *db = setup_test_db();

    /* Scenario A: Caller opens tx, practice_start_balanced succeeds via SAVEPOINT */
    exec_sql(db, "BEGIN TRANSACTION;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller transaction is active (autocommit == 0)");

    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) VALUES (10, 'caller_u1', 'h', 'USER');");

    char *json_out = NULL;
    int att_id = practice_start_balanced(db, 10, 1, 1, 1, NULL, 4, &json_out);
    TEST_ASSERT(att_id > 0, "practice_start_balanced succeeds inside caller transaction");
    TEST_ASSERT(json_out != NULL, "json_out created inside caller transaction");
    free(json_out);

    /* Caller transaction must still be active and uncommitted */
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller transaction remains active after success");

    /* Caller commits outer transaction */
    exec_sql(db, "COMMIT;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit is 1 after caller COMMIT");
    TEST_ASSERT(count_rows_where(db, "users", "id = 10") == 1, "Caller inserted user 10 persisted");
    TEST_ASSERT(count_rows_where(db, "exam_attempts", "id = 1") == 1, "Attempt persisted");

    /* Scenario B: Caller opens tx, practice_start_balanced FAILS (insufficient questions) */
    exec_sql(db, "BEGIN TRANSACTION;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller second transaction active");

    exec_sql(db, "INSERT INTO users (id, username, password_hash, role) VALUES (11, 'caller_u2', 'h', 'USER');");

    json_out = (char *)0x1234;
    int err_rc = practice_start_balanced(db, 11, 1, 1, 1, NULL, 40, &json_out);
    TEST_ASSERT(err_rc == PRACTICE_ERR_INSUFFICIENT_QUESTIONS,
                "Returns PRACTICE_ERR_INSUFFICIENT_QUESTIONS on failure inside caller tx");
    TEST_ASSERT(json_out == NULL, "json_out set to NULL on failure");

    /* Caller transaction must STILL be active and user 11 must NOT be rolled back */
    TEST_ASSERT(sqlite3_get_autocommit(db) == 0, "Caller transaction remains active after inner failure");
    TEST_ASSERT(count_rows_where(db, "users", "id = 11") == 1, "Caller user 11 is NOT affected by inner rollback");
    TEST_ASSERT(count_rows_where(db, "exam_attempts", "user_id = 11") == 0, "No attempt created for user 11");

    /* Caller rolls back outer transaction */
    exec_sql(db, "ROLLBACK;");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit restored to 1 after caller ROLLBACK");
    TEST_ASSERT(count_rows_where(db, "users", "id = 11") == 0, "User 11 rolled back by caller ROLLBACK");

    sqlite3_close(db);
}

static void test_fault_injection_exam_attempts(void)
{
    printf("\n--- Test: fault_injection_exam_attempts ---\n");
    sqlite3 *db = setup_test_db();

    /* Fault injection: abort any INSERT on exam_attempts */
    exec_sql(db, "CREATE TRIGGER fault_exam_attempts BEFORE INSERT ON exam_attempts "
                 "BEGIN SELECT RAISE(ABORT, 'Fault injection: exam_attempts insert failure'); END;");

    char *json_out = (char *)0x5678;
    int rc = practice_start_balanced(db, 1, 1, 1, 1, NULL, 4, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_DATABASE, "Returns PRACTICE_ERR_DATABASE when exam_attempts INSERT fails");
    TEST_ASSERT(json_out == NULL, "json_out is NULL");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit is 1 (transaction rolled back)");
    TEST_ASSERT(count_rows(db, "exam_attempts") == 0, "0 rows in exam_attempts (no partial attempt)");
    TEST_ASSERT(count_rows(db, "practice_questions") == 0, "0 rows in practice_questions");

    sqlite3_close(db);
}

static void test_fault_injection_practice_questions(void)
{
    printf("\n--- Test: fault_injection_practice_questions ---\n");
    sqlite3 *db = setup_test_db();

    /* Fault injection: abort INSERT on practice_questions at position = 2 */
    exec_sql(db, "CREATE TRIGGER fault_pq_pos2 BEFORE INSERT ON practice_questions "
                 "WHEN NEW.position = 2 "
                 "BEGIN SELECT RAISE(ABORT, 'Fault injection: practice_questions position 2'); END;");

    char *json_out = (char *)0x9abc;
    int rc = practice_start_balanced(db, 1, 1, 1, 1, NULL, 4, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_DATABASE, "Returns PRACTICE_ERR_DATABASE when practice_questions INSERT fails");
    TEST_ASSERT(json_out == NULL, "json_out is NULL");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit is 1 (transaction rolled back)");

    /* CRITICAL: verify attempt was rolled back! NO orphan attempt in exam_attempts! */
    TEST_ASSERT(count_rows(db, "exam_attempts") == 0, "No orphan attempt in exam_attempts after PQ failure");
    TEST_ASSERT(count_rows(db, "practice_questions") == 0, "No partial questions in practice_questions");

    sqlite3_close(db);
}

static void test_fault_injection_answers_query(void)
{
    printf("\n--- Test: fault_injection_answers_query ---\n");
    sqlite3 *db = setup_test_db();

    /* Fault injection: drop answers table to cause statement prepare failure */
    exec_sql(db, "DROP TABLE answers;");

    char *json_out = (char *)0xdef0;
    int rc = practice_start_balanced(db, 1, 1, 1, 1, NULL, 4, &json_out);
    TEST_ASSERT(rc == PRACTICE_ERR_DATABASE, "Returns PRACTICE_ERR_DATABASE when answers query fails");
    TEST_ASSERT(json_out == NULL, "json_out is NULL");
    TEST_ASSERT(sqlite3_get_autocommit(db) == 1, "Autocommit is 1 (transaction rolled back)");
    TEST_ASSERT(count_rows(db, "exam_attempts") == 0, "0 rows in exam_attempts (clean rollback)");
    TEST_ASSERT(count_rows(db, "practice_questions") == 0, "0 rows in practice_questions");

    sqlite3_close(db);
}

int main(void)
{
    printf("====================================================================\n");
    printf("UNIT TESTS: Balanced Practice Integration (Task 06.7)\n");
    printf("====================================================================\n");

    test_practice_start_balanced_success();
    test_practice_start_balanced_distribution();
    test_practice_start_balanced_filters();
    test_practice_start_balanced_insufficient_questions();
    test_practice_start_balanced_zero_questions();
    test_practice_start_balanced_invalid_user();
    test_practice_start_balanced_invalid_params();
    test_practice_start_balanced_security_no_leak();
    test_practice_submit_with_balanced_attempt();
    test_practice_start_traditional_regression();
    test_foreign_key_integrity();
    test_caller_transaction_handling();
    test_fault_injection_exam_attempts();
    test_fault_injection_practice_questions();
    test_fault_injection_answers_query();

    printf("\n====================================================================\n");
    printf("RESULT: %d / %d tests passed\n", g_tests_passed, g_tests_run);
    printf("====================================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
