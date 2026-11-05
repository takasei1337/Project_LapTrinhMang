#include "question_selector.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

/*
 * ============================================================================
 * Internal Structures
 * ============================================================================
 */

typedef struct {
    int topic_id;
    char topic_name[256];
    int available_count;
    int quota;
} TopicInfo;

typedef struct {
    int id;
    char *content;
    int topic_id;
    char topic_name[256];
    char difficulty[32];
} SelectedQuestion;

/*
 * ============================================================================
 * Helper Functions
 * ============================================================================
 */

static void init_prng_once(void)
{
    static int initialized = 0;
    if (!initialized) {
        srand((unsigned int)time(NULL) ^ 0x5A5A5A5AU);
        initialized = 1;
    }
}

static int is_valid_difficulty(const char *diff)
{
    if (diff == NULL || diff[0] == '\0') {
        return 1;
    }
    return (strcmp(diff, "EASY") == 0 ||
            strcmp(diff, "MEDIUM") == 0 ||
            strcmp(diff, "HARD") == 0);
}

static int check_entity_exists(sqlite3 *db, const char *table, int id)
{
    char sql[128];
    snprintf(sql, sizeof(sql), "SELECT 1 FROM %s WHERE id = ?;", table);
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return -1;
    }
    sqlite3_bind_int(stmt, 1, id);
    int exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static void shuffle_topics(TopicInfo *topics, int n)
{
    if (n <= 1) return;
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        TopicInfo tmp = topics[i];
        topics[i] = topics[j];
        topics[j] = tmp;
    }
}

static void shuffle_questions(SelectedQuestion *arr, int n)
{
    if (n <= 1) return;
    for (int i = n - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        SelectedQuestion tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/*
 * ============================================================================
 * Thuật toán Phân phối Quota Cân Bằng (Water-filling Balanced Distribution)
 * ============================================================================
 */
static void distribute_quota(TopicInfo *topics, int k, int target)
{
    for (int i = 0; i < k; i++) {
        topics[i].quota = 0;
    }
    int remaining = target;

    while (remaining > 0) {
        int active_count = 0;
        for (int i = 0; i < k; i++) {
            if (topics[i].quota < topics[i].available_count) {
                active_count++;
            }
        }
        if (active_count == 0) {
            break;
        }

        int share = remaining / active_count;
        if (share > 0) {
            for (int i = 0; i < k && remaining > 0; i++) {
                int cap = topics[i].available_count - topics[i].quota;
                if (cap > 0) {
                    int add = (cap < share) ? cap : share;
                    topics[i].quota += add;
                    remaining -= add;
                }
            }
        } else {
            /* Khi remaining < active_count: phân bổ 1 câu cho 'remaining' topic đầu tiên */
            for (int i = 0; i < k && remaining > 0; i++) {
                if (topics[i].quota < topics[i].available_count) {
                    topics[i].quota += 1;
                    remaining -= 1;
                }
            }
        }
    }
}

/*
 * ============================================================================
 * API Implementation
 * ============================================================================
 */

int question_selector_balanced(sqlite3 *db,
                               int subject_id,
                               int chapter_id,
                               int lesson_id,
                               const char *difficulty,
                               int num_questions,
                               char **out_json)
{
    return question_selector_balanced_ex(db, subject_id, chapter_id, lesson_id, 0,
                                         difficulty, num_questions, out_json);
}

int question_selector_balanced_ex(sqlite3 *db,
                                  int subject_id,
                                  int chapter_id,
                                  int lesson_id,
                                  int topic_id,
                                  const char *difficulty,
                                  int num_questions,
                                  char **out_json)
{
    init_prng_once();

    if (out_json != NULL) {
        *out_json = NULL;
    }

    /* 1. Kiểm tra tham số cơ bản */
    if (db == NULL || out_json == NULL || num_questions <= 0) {
        return QUESTION_SELECTOR_ERR_INVALID_PARAM;
    }

    if (difficulty != NULL && difficulty[0] != '\0' && !is_valid_difficulty(difficulty)) {
        return QUESTION_SELECTOR_ERR_INVALID_PARAM;
    }

    /* 2. Kiểm tra sự tồn tại của các thực thể phân cấp nếu ID > 0 */
    if (subject_id > 0) {
        int ex = check_entity_exists(db, "subjects", subject_id);
        if (ex == 0) return QUESTION_SELECTOR_ERR_NOT_FOUND;
        if (ex < 0) return QUESTION_SELECTOR_ERR_DATABASE;
    }
    if (chapter_id > 0) {
        int ex = check_entity_exists(db, "chapters", chapter_id);
        if (ex == 0) return QUESTION_SELECTOR_ERR_NOT_FOUND;
        if (ex < 0) return QUESTION_SELECTOR_ERR_DATABASE;
    }
    if (lesson_id > 0) {
        int ex = check_entity_exists(db, "lessons", lesson_id);
        if (ex == 0) return QUESTION_SELECTOR_ERR_NOT_FOUND;
        if (ex < 0) return QUESTION_SELECTOR_ERR_DATABASE;
    }
    if (topic_id > 0) {
        int ex = check_entity_exists(db, "topics", topic_id);
        if (ex == 0) return QUESTION_SELECTOR_ERR_NOT_FOUND;
        if (ex < 0) return QUESTION_SELECTOR_ERR_DATABASE;
    }

    /* 3. Xây dựng câu truy vấn tìm các Topic hợp lệ và số câu hỏi khả dụng của mỗi Topic */
    char where_sql[512] = "WHERE 1=1 ";
    int param_sub = (subject_id > 0);
    int param_chap = (chapter_id > 0);
    int param_les = (lesson_id > 0);
    int param_top = (topic_id > 0);
    int param_diff = (difficulty != NULL && difficulty[0] != '\0');

    if (param_sub) strcat(where_sql, "AND s.id = ? ");
    if (param_chap) strcat(where_sql, "AND c.id = ? ");
    if (param_les) strcat(where_sql, "AND l.id = ? ");
    if (param_top) strcat(where_sql, "AND t.id = ? ");
    if (param_diff) strcat(where_sql, "AND q.difficulty = ? ");

    char topic_query[1024];
    snprintf(topic_query, sizeof(topic_query),
             "SELECT t.id, t.name, COUNT(q.id) AS valid_count "
             "FROM topics t "
             "JOIN lessons l ON t.lesson_id = l.id "
             "JOIN chapters c ON l.chapter_id = c.id "
             "JOIN subjects s ON c.subject_id = s.id "
             "JOIN questions q ON q.topic_id = t.id "
             "%s "
             "  AND (SELECT COUNT(*) FROM answers a WHERE a.question_id = q.id) >= 2 "
             "  AND (SELECT SUM(a.is_correct) FROM answers a WHERE a.question_id = q.id) = 1 "
             "GROUP BY t.id, t.name "
             "HAVING valid_count > 0 "
             "ORDER BY t.id ASC;", where_sql);

    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, topic_query, -1, &stmt, NULL) != SQLITE_OK) {
        return QUESTION_SELECTOR_ERR_DATABASE;
    }

    int bidx = 1;
    if (param_sub) sqlite3_bind_int(stmt, bidx++, subject_id);
    if (param_chap) sqlite3_bind_int(stmt, bidx++, chapter_id);
    if (param_les) sqlite3_bind_int(stmt, bidx++, lesson_id);
    if (param_top) sqlite3_bind_int(stmt, bidx++, topic_id);
    if (param_diff) sqlite3_bind_text(stmt, bidx++, difficulty, -1, SQLITE_STATIC);

    size_t topic_cap = 16;
    size_t topic_count = 0;
    TopicInfo *topics = (TopicInfo *)malloc(sizeof(TopicInfo) * topic_cap);
    if (!topics) {
        sqlite3_finalize(stmt);
        return QUESTION_SELECTOR_ERR_MEMORY;
    }

    int total_available = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (topic_count >= topic_cap) {
            topic_cap *= 2;
            TopicInfo *new_topics = (TopicInfo *)realloc(topics, sizeof(TopicInfo) * topic_cap);
            if (!new_topics) {
                free(topics);
                sqlite3_finalize(stmt);
                return QUESTION_SELECTOR_ERR_MEMORY;
            }
            topics = new_topics;
        }
        topics[topic_count].topic_id = sqlite3_column_int(stmt, 0);
        const char *tname = (const char *)sqlite3_column_text(stmt, 1);
        snprintf(topics[topic_count].topic_name, sizeof(topics[topic_count].topic_name),
                 "%s", tname ? tname : "");
        int avail = sqlite3_column_int(stmt, 2);
        topics[topic_count].available_count = avail;
        topics[topic_count].quota = 0;
        total_available += avail;
        topic_count++;
    }
    sqlite3_finalize(stmt);

    /* 4. Xử lý trường hợp không có câu hỏi hợp lệ nào (total_available == 0) */
    if (topic_count == 0 || total_available == 0) {
        free(topics);

        cJSON *root = cJSON_CreateObject();
        if (!root) return QUESTION_SELECTOR_ERR_MEMORY;
        cJSON_AddNumberToObject(root, "requested", num_questions);
        cJSON_AddNumberToObject(root, "selected", 0);
        cJSON_AddNumberToObject(root, "shortage", num_questions);
        cJSON_AddArrayToObject(root, "questions");
        cJSON_AddArrayToObject(root, "topic_distribution");

        char *json_out = cJSON_PrintUnformatted(root);
        cJSON_Delete(root);
        if (!json_out) return QUESTION_SELECTOR_ERR_MEMORY;

        *out_json = json_out;
        return QUESTION_SELECTOR_SUCCESS;
    }

    /* 5. Ngẫu nhiên hóa thứ tự Topic trước khi phân phối quota */
    shuffle_topics(topics, (int)topic_count);

    /* 6. Phân phối quota cân bằng */
    int target = (num_questions < total_available) ? num_questions : total_available;
    distribute_quota(topics, (int)topic_count, target);

    /* 7. Trích xuất ngẫu nhiên câu hỏi trong từng Topic theo quota đã cấp */
    SelectedQuestion *q_list = (SelectedQuestion *)malloc(sizeof(SelectedQuestion) * target);
    if (!q_list) {
        free(topics);
        return QUESTION_SELECTOR_ERR_MEMORY;
    }

    int total_selected = 0;
    for (size_t i = 0; i < topic_count; i++) {
        if (topics[i].quota <= 0) {
            continue;
        }

        char fetch_sql[1024];
        snprintf(fetch_sql, sizeof(fetch_sql),
                 "SELECT q.id, q.content, q.difficulty "
                 "FROM questions q "
                 "WHERE q.topic_id = ? "
                 "  AND (SELECT COUNT(*) FROM answers a WHERE a.question_id = q.id) >= 2 "
                 "  AND (SELECT SUM(a.is_correct) FROM answers a WHERE a.question_id = q.id) = 1 "
                 "  %s "
                 "ORDER BY RANDOM() "
                 "LIMIT ?;",
                 (param_diff ? "AND q.difficulty = ?" : ""));

        sqlite3_stmt *q_stmt = NULL;
        if (sqlite3_prepare_v2(db, fetch_sql, -1, &q_stmt, NULL) != SQLITE_OK) {
            for (int k = 0; k < total_selected; k++) {
                free(q_list[k].content);
            }
            free(q_list);
            free(topics);
            return QUESTION_SELECTOR_ERR_DATABASE;
        }

        int qbidx = 1;
        sqlite3_bind_int(q_stmt, qbidx++, topics[i].topic_id);
        if (param_diff) {
            sqlite3_bind_text(q_stmt, qbidx++, difficulty, -1, SQLITE_STATIC);
        }
        sqlite3_bind_int(q_stmt, qbidx++, topics[i].quota);

        while (sqlite3_step(q_stmt) == SQLITE_ROW && total_selected < target) {
            q_list[total_selected].id = sqlite3_column_int(q_stmt, 0);
            const char *cnt = (const char *)sqlite3_column_text(q_stmt, 1);
            size_t clen = cnt ? strlen(cnt) : 0;
            char *cnt_copy = (char *)malloc(clen + 1);
            if (cnt_copy) {
                memcpy(cnt_copy, cnt ? cnt : "", clen + 1);
            }
            q_list[total_selected].content = cnt_copy;
            q_list[total_selected].topic_id = topics[i].topic_id;
            snprintf(q_list[total_selected].topic_name, sizeof(q_list[total_selected].topic_name),
                     "%s", topics[i].topic_name);
            const char *diff_text = (const char *)sqlite3_column_text(q_stmt, 2);
            snprintf(q_list[total_selected].difficulty, sizeof(q_list[total_selected].difficulty),
                     "%s", diff_text ? diff_text : "");

            total_selected++;
        }
        sqlite3_finalize(q_stmt);
    }

    /* 8. Ngẫu nhiên hóa thứ tự danh sách câu hỏi cuối cùng */
    shuffle_questions(q_list, total_selected);

    /* 9. Đóng gói kết quả JSON */
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        for (int k = 0; k < total_selected; k++) free(q_list[k].content);
        free(q_list);
        free(topics);
        return QUESTION_SELECTOR_ERR_MEMORY;
    }

    cJSON_AddNumberToObject(root, "requested", num_questions);
    cJSON_AddNumberToObject(root, "selected", total_selected);
    cJSON_AddNumberToObject(root, "shortage", num_questions - total_selected);

    cJSON *q_array = cJSON_AddArrayToObject(root, "questions");
    for (int k = 0; k < total_selected; k++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", q_list[k].id);
        cJSON_AddStringToObject(item, "content", q_list[k].content ? q_list[k].content : "");
        cJSON_AddNumberToObject(item, "topic_id", q_list[k].topic_id);
        cJSON_AddStringToObject(item, "topic_name", q_list[k].topic_name);
        cJSON_AddStringToObject(item, "difficulty", q_list[k].difficulty);
        cJSON_AddItemToArray(q_array, item);
    }

    cJSON *dist_array = cJSON_AddArrayToObject(root, "topic_distribution");
    for (size_t i = 0; i < topic_count; i++) {
        if (topics[i].quota > 0) {
            cJSON *d_item = cJSON_CreateObject();
            cJSON_AddNumberToObject(d_item, "topic_id", topics[i].topic_id);
            cJSON_AddStringToObject(d_item, "topic_name", topics[i].topic_name);
            cJSON_AddNumberToObject(d_item, "count", topics[i].quota);
            cJSON_AddItemToArray(dist_array, d_item);
        }
    }

    /* 10. Giải phóng bộ nhớ tạm */
    for (int k = 0; k < total_selected; k++) {
        free(q_list[k].content);
    }
    free(q_list);
    free(topics);

    char *json_out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_out) {
        return QUESTION_SELECTOR_ERR_MEMORY;
    }

    *out_json = json_out;
    return QUESTION_SELECTOR_SUCCESS;
}

