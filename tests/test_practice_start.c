#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

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
 * Nap du lieu mau:
 * - 2 users: id 1 (alice), id 2 (bob)
 * - 8 cau hoi hop le (>= 2 dap an, dung 1 is_correct = 1):
 *   + q1: Network, EASY
 *   + q2: Network, EASY
 *   + q3: Network, MEDIUM
 *   + q4: Network, HARD
 *   + q5: OS, EASY
 *   + q6: OS, MEDIUM
 *   + q7: OS, HARD
 *   + q8: Database, EASY
 * - 4 cau hoi khong hop le (phai bi loai bo):
 *   + q9: Network, EASY (chi co 1 dap an)
 *   + q10: Network, EASY (khong co dap an dung)
 *   + q11: Network, EASY (co 2 dap an dung)
 *   + q12: Network, EASY (khong co dap an nao)
 */
static void seed_test_data(sqlite3 *db)
{
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (1, 'alice', 'hash1');");
    exec_sql(db, "INSERT INTO users (id, username, password_hash) VALUES (2, 'bob', 'hash2');");

    /* q1: Network, EASY */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (1, 'Net Q1?', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (1, 1, 'Ans 1A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (2, 1, 'Ans 1B', 0);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (3, 1, 'Ans 1C', 0);");

    /* q2: Network, EASY */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (2, 'Net Q2?', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (4, 2, 'Ans 2A', 0);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (5, 2, 'Ans 2B', 1);");

    /* q3: Network, MEDIUM */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (3, 'Net Q3?', 'Network', 'MEDIUM');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (6, 3, 'Ans 3A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (7, 3, 'Ans 3B', 0);");

    /* q4: Network, HARD */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (4, 'Net Q4?', 'Network', 'HARD');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (8, 4, 'Ans 4A', 0);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (9, 4, 'Ans 4B', 1);");

    /* q5: OS, EASY */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (5, 'OS Q5?', 'OS', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (10, 5, 'Ans 5A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (11, 5, 'Ans 5B', 0);");

    /* q6: OS, MEDIUM */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (6, 'OS Q6?', 'OS', 'MEDIUM');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (12, 6, 'Ans 6A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (13, 6, 'Ans 6B', 0);");

    /* q7: OS, HARD */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (7, 'OS Q7?', 'OS', 'HARD');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (14, 7, 'Ans 7A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (15, 7, 'Ans 7B', 0);");

    /* q8: Database, EASY */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (8, 'DB Q8?', 'Database', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (16, 8, 'Ans 8A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (17, 8, 'Ans 8B', 0);");

    /* q9: Chi co 1 dap an */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (9, 'Invalid Q9 (1 answer)', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (18, 9, 'Ans 9A', 1);");

    /* q10: Khong co dap an dung */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (10, 'Invalid Q10 (0 correct)', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (19, 10, 'Ans 10A', 0);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (20, 10, 'Ans 10B', 0);");

    /* q11: Co 2 dap an dung */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (11, 'Invalid Q11 (2 correct)', 'Network', 'EASY');");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (21, 11, 'Ans 11A', 1);");
    exec_sql(db, "INSERT INTO answers (id, question_id, content, is_correct) VALUES (22, 11, 'Ans 11B', 1);");

    /* q12: Khong co dap an nao */
    exec_sql(db, "INSERT INTO questions (id, content, topic, difficulty) VALUES (12, 'Invalid Q12 (0 answers)', 'Network', 'EASY');");
}

/* Kiem tra de quy cay cJSON khong chua cac truong lien quan toi dap an dung */
static int json_has_secret_field(cJSON *node)
{
    if (!node) {
        return 0;
    }
    if (node->string) {
        if (strcmp(node->string, "is_correct") == 0 ||
            strcmp(node->string, "correct_answer_id") == 0 ||
            strstr(node->string, "correct") != NULL) {
            return 1;
        }
    }
    for (cJSON *child = node->child; child; child = child->next) {
        if (json_has_secret_field(child)) {
            return 1;
        }
    }
    return 0;
}

/* =========================================================================
 * Test Suite 1: Bat dau luyen tap thanh cong va kiem tra toan ven
 * (Yeu cau 1, 2, 3, 4, 5, 6, 7, 8, 9)
 * ========================================================================= */
static void test_practice_start_success(sqlite3 *db)
{
    printf("\n--- Test Suite 1: Bat dau luyen tap thanh cong ---\n");

    char *out_json = NULL;
    int attempt_id = practice_start(db, 1, NULL, NULL, 3, &out_json);

    /* 1. Bat dau luyen tap thanh cong */
    TEST_ASSERT(attempt_id > 0, "1. practice_start() tra ve attempt_id hop le (> 0)");
    /* 2. attempt_id > 0 */
    TEST_ASSERT(out_json != NULL, "2. Chuoi out_questions_json khong NULL khi thanh cong");

    /* 3 & 4. Kiem tra bang exam_attempts */
    const char *sql_att = "SELECT user_id, room_id, mode, score, submitted_at FROM exam_attempts WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql_att, -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement kiem tra exam_attempts");

    int user_id_db = -1;
    int room_id_is_null = 0;
    char mode_str[32] = {0};
    int score_is_null = 0;
    int submitted_at_is_null = 0;

    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, attempt_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            user_id_db = sqlite3_column_int(stmt, 0);
            room_id_is_null = (sqlite3_column_type(stmt, 1) == SQLITE_NULL);
            const unsigned char *m = sqlite3_column_text(stmt, 2);
            if (m) {
                strncpy(mode_str, (const char *)m, sizeof(mode_str) - 1);
            }
            score_is_null = (sqlite3_column_type(stmt, 3) == SQLITE_NULL);
            submitted_at_is_null = (sqlite3_column_type(stmt, 4) == SQLITE_NULL);
        }
        sqlite3_finalize(stmt);
    }

    TEST_ASSERT(user_id_db == 1, "Attempt thuoc dung user_id = 1");
    TEST_ASSERT(room_id_is_null, "3a. Attempt co room_id IS NULL");
    TEST_ASSERT(strcmp(mode_str, "PRACTICE") == 0, "3b. Attempt co mode = 'PRACTICE'");
    TEST_ASSERT(score_is_null, "4a. Attempt co score ban dau la NULL");
    TEST_ASSERT(submitted_at_is_null, "4b. Attempt co submitted_at ban dau la NULL");

    /* 5. So ban ghi trong practice_questions bang so cau yeu cau */
    const char *sql_pq_cnt = "SELECT COUNT(*) FROM practice_questions WHERE attempt_id = ?;";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_pq_cnt, -1, &stmt, NULL);
    int pq_count = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, attempt_id);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            pq_count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(pq_count == 3, "5. So ban ghi trong practice_questions bang dung so cau yeu cau (3)");

    /* 6. position bat dau tu 1 va khong trung (1, 2, 3) */
    const char *sql_pq_pos = "SELECT position, question_id FROM practice_questions WHERE attempt_id = ? ORDER BY position ASC;";
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, sql_pq_pos, -1, &stmt, NULL);
    int expected_pos = 1;
    int pos_ok = 1;
    int db_q_ids[3] = {0};
    int q_idx = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        sqlite3_bind_int(stmt, 1, attempt_id);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int pos = sqlite3_column_int(stmt, 0);
            int q_id = sqlite3_column_int(stmt, 1);
            if (pos != expected_pos) {
                pos_ok = 0;
            }
            if (q_idx < 3) {
                db_q_ids[q_idx++] = q_id;
            }
            expected_pos++;
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(pos_ok && expected_pos == 4, "6. position bat dau tu 1, tang dan lien tuc va khong trung");

    /* 7. JSON hop le va total_questions chinh xac */
    cJSON *parsed = cJSON_Parse(out_json);
    TEST_ASSERT(parsed != NULL, "7a. cJSON_Parse() thanh cong tren out_questions_json");

    int json_attempt_id = 0;
    int json_total = 0;
    cJSON *q_array = NULL;
    if (parsed) {
        cJSON *item = cJSON_GetObjectItemCaseSensitive(parsed, "attempt_id");
        if (item && cJSON_IsNumber(item)) {
            json_attempt_id = item->valueint;
        }
        item = cJSON_GetObjectItemCaseSensitive(parsed, "total_questions");
        if (item && cJSON_IsNumber(item)) {
            json_total = item->valueint;
        }
        q_array = cJSON_GetObjectItemCaseSensitive(parsed, "questions");
    }
    TEST_ASSERT(json_attempt_id == attempt_id, "7b. attempt_id trong JSON khop voi attempt_id tra ve");
    TEST_ASSERT(json_total == 3, "7c. total_questions trong JSON bang 3");
    TEST_ASSERT(q_array != NULL && cJSON_IsArray(q_array), "7d. 'questions' trong JSON la array");
    TEST_ASSERT(cJSON_GetArraySize(q_array) == 3, "7e. Array questions co 3 phan tu");

    /* 8. Bo cau hoi trong JSON khop voi bo cau hoi trong database */
    int json_match_db = 1;
    for (int i = 0; i < 3; i++) {
        cJSON *q_item = cJSON_GetArrayItem(q_array, i);
        if (!q_item) {
            json_match_db = 0;
            break;
        }
        cJSON *qid = cJSON_GetObjectItemCaseSensitive(q_item, "question_id");
        if (!qid || !cJSON_IsNumber(qid) || qid->valueint != db_q_ids[i]) {
            json_match_db = 0;
            break;
        }
    }
    TEST_ASSERT(json_match_db, "8. Bo cau hoi trong JSON khop dung thu tu va question_id trong database");

    /* 9. JSON khong co is_correct hoac correct_answer_id */
    int secret_found = json_has_secret_field(parsed);
    TEST_ASSERT(secret_found == 0, "9. JSON khong chua bat ky truong is_correct hoac correct_answer_id nao");

    if (parsed) {
        cJSON_Delete(parsed);
    }
    free(out_json);
}

/* =========================================================================
 * Test Suite 2: Loc theo topic va difficulty
 * (Yeu cau 10, 11, 12)
 * ========================================================================= */
static void test_practice_filtering(sqlite3 *db)
{
    printf("\n--- Test Suite 2: Loc theo topic va difficulty ---\n");

    /* 10. Loc theo topic "Network" */
    char *json_net = NULL;
    int att_net = practice_start(db, 1, "Network", NULL, 2, &json_net);
    TEST_ASSERT(att_net > 0 && json_net != NULL, "10a. practice_start() loc theo topic 'Network' thanh cong");

    cJSON *parsed_net = cJSON_Parse(json_net);
    int all_network = 1;
    if (parsed_net) {
        cJSON *qs = cJSON_GetObjectItemCaseSensitive(parsed_net, "questions");
        int sz = cJSON_GetArraySize(qs);
        for (int i = 0; i < sz; i++) {
            cJSON *q = cJSON_GetArrayItem(qs, i);
            cJSON *top = cJSON_GetObjectItemCaseSensitive(q, "topic");
            if (!top || !cJSON_IsString(top) || strcmp(top->valuestring, "Network") != 0) {
                all_network = 0;
            }
        }
        cJSON_Delete(parsed_net);
    } else {
        all_network = 0;
    }
    free(json_net);
    TEST_ASSERT(all_network, "10b. Tat ca cau hoi duoc chon deu co topic la 'Network'");

    /* 11. Loc theo difficulty "HARD" */
    char *json_hard = NULL;
    int att_hard = practice_start(db, 1, NULL, "HARD", 2, &json_hard);
    TEST_ASSERT(att_hard > 0 && json_hard != NULL, "11a. practice_start() loc theo difficulty 'HARD' thanh cong");

    cJSON *parsed_hard = cJSON_Parse(json_hard);
    int all_hard = 1;
    if (parsed_hard) {
        cJSON *qs = cJSON_GetObjectItemCaseSensitive(parsed_hard, "questions");
        int sz = cJSON_GetArraySize(qs);
        for (int i = 0; i < sz; i++) {
            cJSON *q = cJSON_GetArrayItem(qs, i);
            cJSON *diff = cJSON_GetObjectItemCaseSensitive(q, "difficulty");
            if (!diff || !cJSON_IsString(diff) || strcmp(diff->valuestring, "HARD") != 0) {
                all_hard = 0;
            }
        }
        cJSON_Delete(parsed_hard);
    } else {
        all_hard = 0;
    }
    free(json_hard);
    TEST_ASSERT(all_hard, "11b. Tat ca cau hoi duoc chon deu co difficulty la 'HARD'");

    /* 12. Loc dong thoi topic 'OS' va difficulty 'MEDIUM' */
    char *json_both = NULL;
    int att_both = practice_start(db, 1, "OS", "MEDIUM", 1, &json_both);
    TEST_ASSERT(att_both > 0 && json_both != NULL, "12a. practice_start() loc dong thoi topic 'OS' & difficulty 'MEDIUM' thanh cong");

    cJSON *parsed_both = cJSON_Parse(json_both);
    int match_both = 0;
    if (parsed_both) {
        cJSON *qs = cJSON_GetObjectItemCaseSensitive(parsed_both, "questions");
        if (cJSON_GetArraySize(qs) == 1) {
            cJSON *q = cJSON_GetArrayItem(qs, 0);
            cJSON *top = cJSON_GetObjectItemCaseSensitive(q, "topic");
            cJSON *diff = cJSON_GetObjectItemCaseSensitive(q, "difficulty");
            if (top && diff && strcmp(top->valuestring, "OS") == 0 && strcmp(diff->valuestring, "MEDIUM") == 0) {
                match_both = 1;
            }
        }
        cJSON_Delete(parsed_both);
    }
    free(json_both);
    TEST_ASSERT(match_both, "12b. Cau hoi duoc chon dung ca topic 'OS' va difficulty 'MEDIUM'");
}

/* =========================================================================
 * Test Suite 3: Tinh nguyen tu va Rollback khi loi logic
 * (Yeu cau 13, 14, 24 va Kiem tra tinh nguyen tu)
 * ========================================================================= */
static void test_practice_rollback_and_failures(sqlite3 *db)
{
    printf("\n--- Test Suite 3: Tinh nguyen tu va Rollback khi loi ---\n");

    int attempts_before = count_rows(db, "exam_attempts");
    int pq_before = count_rows(db, "practice_questions");

    /* 13. Khong du cau hoi (yeu cau 10 cau topic OS trong khi chi co 3 cau hop le) */
    char *out_json = (char *)0x1234;
    int rc = practice_start(db, 1, "OS", NULL, 10, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_ENOUGH_QUESTIONS, "13a. Tra ve PRACTICE_ERR_NOT_ENOUGH_QUESTIONS khi khong du cau hoi");
    TEST_ASSERT(out_json == NULL, "13b. *out_questions_json duoc gan NULL khi loi thieu cau hoi");

    int attempts_after = count_rows(db, "exam_attempts");
    int pq_after = count_rows(db, "practice_questions");
    TEST_ASSERT(attempts_before == attempts_after, "13c. exam_attempts khong bi ghi dở dang khi khong du cau hoi");
    TEST_ASSERT(pq_before == pq_after, "13d. practice_questions khong bi ghi dở dang khi khong du cau hoi");

    /* 14. User khong ton tai (user_id = 9999) */
    out_json = (char *)0x1234;
    rc = practice_start(db, 9999, "Network", "EASY", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_FOUND, "14a. Tra ve loi (PRACTICE_ERR_NOT_FOUND) khi user khong ton tai");
    TEST_ASSERT(out_json == NULL, "14b. *out_questions_json duoc gan NULL khi user khong ton tai");

    attempts_after = count_rows(db, "exam_attempts");
    pq_after = count_rows(db, "practice_questions");
    TEST_ASSERT(attempts_before == attempts_after, "14c. exam_attempts khong thay doi khi user khong ton tai");
    TEST_ASSERT(pq_before == pq_after, "14d. practice_questions khong thay doi khi user khong ton tai");
}

/* =========================================================================
 * Test Suite 4: Kiem tra validation tham so dau vao
 * (Yeu cau 15, 16, 17, 18, 19, 20, 24)
 * ========================================================================= */
static void test_practice_invalid_params(sqlite3 *db)
{
    printf("\n--- Test Suite 4: Kiem tra validation tham so ---\n");

    char *out_json = NULL;

    /* 15. db == NULL */
    int rc = practice_start(NULL, 1, "Network", "EASY", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM, "15. Tra ve PRACTICE_ERR_INVALID_PARAM khi db == NULL");

    /* 16. out_questions_json == NULL */
    rc = practice_start(db, 1, "Network", "EASY", 1, NULL);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM, "16. Tra ve PRACTICE_ERR_INVALID_PARAM khi out_questions_json == NULL");

    /* 17. user_id <= 0 */
    out_json = (char *)0x1234;
    rc = practice_start(db, 0, "Network", "EASY", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "17a. Tra ve PRACTICE_ERR_INVALID_PARAM khi user_id = 0");

    out_json = (char *)0x1234;
    rc = practice_start(db, -5, "Network", "EASY", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "17b. Tra ve PRACTICE_ERR_INVALID_PARAM khi user_id < 0");

    /* 18. num_questions <= 0 hoac > 50 */
    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "Network", "EASY", 0, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "18a. Tra ve PRACTICE_ERR_INVALID_PARAM khi num_questions = 0");

    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "Network", "EASY", -1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "18b. Tra ve PRACTICE_ERR_INVALID_PARAM khi num_questions < 0");

    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "Network", "EASY", 51, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "18c. Tra ve PRACTICE_ERR_INVALID_PARAM khi num_questions > 50");

    /* 19. Topic rong */
    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "", "EASY", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "19. Tra ve PRACTICE_ERR_INVALID_PARAM khi topic rong");

    /* 20. Difficulty khong hop le */
    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "Network", "VERY_HARD", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "20a. Tra ve PRACTICE_ERR_INVALID_PARAM khi difficulty khong hop le ('VERY_HARD')");

    out_json = (char *)0x1234;
    rc = practice_start(db, 1, "Network", "", 1, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_INVALID_PARAM && out_json == NULL, "20b. Tra ve PRACTICE_ERR_INVALID_PARAM khi difficulty la chuoi rong");
}

/* =========================================================================
 * Test Suite 5: Loai bo cau hoi khong dat tieu chuan trac nghiem
 * (Yeu cau 21, 22, 23)
 * ========================================================================= */
static void test_practice_filter_invalid_questions(sqlite3 *db)
{
    printf("\n--- Test Suite 5: Loai bo cau hoi khong hop le ---\n");

    /* Trong topic 'Network' va difficulty 'EASY':
     * - Co 2 cau hop le: q1 (3 dap an, 1 dung), q2 (2 dap an, 1 dung)
     * - Co 4 cau khong hop le:
     *   + q9 (1 dap an) -> yeu cau 21
     *   + q10 (0 dap an dung) -> yeu cau 22
     *   + q11 (2 dap an dung) -> yeu cau 23
     *   + q12 (0 dap an) -> yeu cau 21
     * Neu lay 2 cau: phai lay dung q1 va q2.
     * Neu lay 3 cau: phai tra ve PRACTICE_ERR_NOT_ENOUGH_QUESTIONS vi chi co dung 2 cau hop le.
     */
    char *out_json = NULL;
    int att_id = practice_start(db, 1, "Network", "EASY", 2, &out_json);
    TEST_ASSERT(att_id > 0 && out_json != NULL, "Lay 2 cau hop le thanh cong");

    cJSON *parsed = cJSON_Parse(out_json);
    int only_valid_questions = 1;
    if (parsed) {
        cJSON *qs = cJSON_GetObjectItemCaseSensitive(parsed, "questions");
        int sz = cJSON_GetArraySize(qs);
        for (int i = 0; i < sz; i++) {
            cJSON *q = cJSON_GetArrayItem(qs, i);
            cJSON *qid = cJSON_GetObjectItemCaseSensitive(q, "question_id");
            if (qid && cJSON_IsNumber(qid)) {
                int id = qid->valueint;
                /* q9, q10, q11, q12 khong duoc phep xuat hien */
                if (id == 9 || id == 10 || id == 11 || id == 12) {
                    only_valid_questions = 0;
                }
            }
        }
        cJSON_Delete(parsed);
    } else {
        only_valid_questions = 0;
    }
    free(out_json);

    /* 21, 22, 23 */
    TEST_ASSERT(only_valid_questions, "21-23. Cac cau hoi q9 (1 dap an), q10 (0 dung), q11 (2 dung), q12 (0 dap an) da bi loai bo");

    /* Kiem chung lai: hoi 3 cau Network EASY phai tra ve NOT_ENOUGH_QUESTIONS */
    out_json = NULL;
    int rc = practice_start(db, 1, "Network", "EASY", 3, &out_json);
    TEST_ASSERT(rc == PRACTICE_ERR_NOT_ENOUGH_QUESTIONS, "Xac minh chi co dung 2 cau hop le (yeu cau 3 cau se that bai)");
}

/* =========================================================================
 * Test Suite 6: Kiem tra PRAGMA integrity_check va foreign_key_check
 * (Yeu cau 25, 26)
 * ========================================================================= */
static void test_practice_db_integrity(sqlite3 *db)
{
    printf("\n--- Test Suite 6: Kiem tra toan ven CSDL ---\n");

    /* 25. PRAGMA integrity_check = ok */
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, "PRAGMA integrity_check;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement PRAGMA integrity_check");

    int integrity_ok = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *res = sqlite3_column_text(stmt, 0);
            if (res && strcmp((const char *)res, "ok") == 0) {
                integrity_ok = 1;
            }
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(integrity_ok, "25. PRAGMA integrity_check tra ve 'ok'");

    /* 26. PRAGMA foreign_key_check khong co vi pham */
    stmt = NULL;
    rc = sqlite3_prepare_v2(db, "PRAGMA foreign_key_check;", -1, &stmt, NULL);
    TEST_ASSERT(rc == SQLITE_OK && stmt != NULL, "Chuan bi statement PRAGMA foreign_key_check");

    int fk_violations = 0;
    if (rc == SQLITE_OK && stmt != NULL) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fk_violations++;
        }
        sqlite3_finalize(stmt);
    }
    TEST_ASSERT(fk_violations == 0, "26. PRAGMA foreign_key_check khong co vi pham (0 violations)");
}

int main(void)
{
    printf("================================================================\n");
    printf("RUNNING UNIT TESTS FOR PRACTICE_START (TASK 04.4.1)\n");
    printf("================================================================\n");

    sqlite3 *db = setup_test_db();
    if (!db) {
        fprintf(stderr, "FATAL: Could not initialize SQLite test database.\n");
        return EXIT_FAILURE;
    }

    seed_test_data(db);

    test_practice_start_success(db);
    test_practice_filtering(db);
    test_practice_rollback_and_failures(db);
    test_practice_invalid_params(db);
    test_practice_filter_invalid_questions(db);
    test_practice_db_integrity(db);

    sqlite3_close(db);

    printf("\n================================================================\n");
    printf("PRACTICE_START TEST RESULTS: %d/%d assertions passed.\n", g_tests_passed, g_tests_run);
    printf("================================================================\n");

    if (g_tests_passed == g_tests_run && g_tests_run > 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

