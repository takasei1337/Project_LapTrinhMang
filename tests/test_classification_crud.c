#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <cjson/cJSON.h>

#include "classification.h"

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

/*
 * ============================================================================
 * NHÓM 1: SUBJECT CRUD THÀNH CÔNG VÀ XÁC THỰC DỮ LIỆU
 * ============================================================================
 */
static void test_group1_subject_crud(void)
{
    printf("\n=== NHÓM 1: Subject CRUD Thành công ===\n");
    sqlite3 *db = create_in_memory_db();
    TEST_ASSERT(db != NULL, "1a. Khởi tạo DB in-memory thành công");

    /* 1. Create Subject */
    int sub1_id = 0;
    int rc = classification_subject_create(db, "CS101", "Lap trinh C", "Mon co so lap trinh", &sub1_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && sub1_id > 0, "1b. classification_subject_create() thành công");

    int sub2_id = 0;
    rc = classification_subject_create(db, "NET201", "Mang may tinh", NULL, &sub2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && sub2_id > 0, "1c. classification_subject_create() với description = NULL thành công");

    /* 2. Get Subject */
    char *json_get = NULL;
    rc = classification_subject_get(db, sub1_id, &json_get);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_get != NULL, "1d. classification_subject_get() thành công");
    if (json_get) {
        cJSON *root = cJSON_Parse(json_get);
        TEST_ASSERT(root != NULL, "1e. JSON chi tiết Subject parse hợp lệ");
        if (root) {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            TEST_ASSERT(code && strcmp(code->valuestring, "CS101") == 0, "1f. Subject code chính xác");
            TEST_ASSERT(name && strcmp(name->valuestring, "Lap trinh C") == 0, "1g. Subject name chính xác");
            TEST_ASSERT(desc && strcmp(desc->valuestring, "Mon co so lap trinh") == 0, "1h. Subject description chính xác");
            cJSON_Delete(root);
        }
        free(json_get);
    }

    /* 3. List Subjects */
    char *json_list = NULL;
    rc = classification_subject_list(db, 10, 0, &json_list);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_list != NULL, "1i. classification_subject_list() thành công");
    if (json_list) {
        cJSON *root = cJSON_Parse(json_list);
        TEST_ASSERT(root != NULL, "1j. JSON danh sách Subject parse hợp lệ");
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            cJSON *count = cJSON_GetObjectItem(root, "count");
            cJSON *arr = cJSON_GetObjectItem(root, "subjects");
            TEST_ASSERT(total && total->valueint == 2, "1k. Total subjects = 2");
            TEST_ASSERT(count && count->valueint == 2, "1l. Count subjects = 2");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 2, "1m. Array size = 2");
            cJSON_Delete(root);
        }
        free(json_list);
    }

    /* 4. Update Subject */
    rc = classification_subject_update(db, sub1_id, "CS101_MOD", "Lap trinh C Nang cao", "Mo ta moi");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "1n. classification_subject_update() thành công");

    /* Kiểm tra sau update */
    json_get = NULL;
    classification_subject_get(db, sub1_id, &json_get);
    if (json_get) {
        cJSON *root = cJSON_Parse(json_get);
        if (root) {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            TEST_ASSERT(code && strcmp(code->valuestring, "CS101_MOD") == 0, "1o. Code đã được cập nhật");
            TEST_ASSERT(name && strcmp(name->valuestring, "Lap trinh C Nang cao") == 0, "1p. Name đã được cập nhật");
            cJSON_Delete(root);
        }
        free(json_get);
    }

    /* 5. Delete Subject (chưa có chapter con) */
    rc = classification_subject_delete(db, sub2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "1q. classification_subject_delete() thành công khi không có ràng buộc");

    /* Xóa lại đối tượng đã xóa -> NOT_FOUND */
    rc = classification_subject_delete(db, sub2_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_NOT_FOUND, "1r. Xóa lại subject đã xóa trả về CLASSIFICATION_ERR_NOT_FOUND");

    TEST_ASSERT(count_fk_violations(db) == 0, "1s. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 2: CHAPTER CRUD THÀNH CÔNG & PHÂN CẤP SUBJECT -> CHAPTER
 * ============================================================================
 */
static void test_group2_chapter_crud(void)
{
    printf("\n=== NHÓM 2: Chapter CRUD Thành công & Phân cấp ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub_id = 0;
    classification_subject_create(db, "NET", "Network Programming", "Mo ta", &sub_id);

    /* 1. Create Chapters */
    int chap1_id = 0, chap2_id = 0;
    int rc = classification_chapter_create(db, sub_id, 1, "Chuong 1: Tong quan", "Mo dau", &chap1_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && chap1_id > 0, "2a. classification_chapter_create() chap 1 thành công");

    rc = classification_chapter_create(db, sub_id, 2, "Chuong 2: Socket TCP", NULL, &chap2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && chap2_id > 0, "2b. classification_chapter_create() chap 2 thành công");

    /* 2. Get Chapter */
    char *json_get = NULL;
    rc = classification_chapter_get(db, chap1_id, &json_get);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_get != NULL, "2c. classification_chapter_get() thành công");
    if (json_get) {
        cJSON *root = cJSON_Parse(json_get);
        TEST_ASSERT(root != NULL, "2d. JSON Chapter hợp lệ");
        if (root) {
            cJSON *sid = cJSON_GetObjectItem(root, "subject_id");
            cJSON *ord = cJSON_GetObjectItem(root, "order_index");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            TEST_ASSERT(sid && sid->valueint == sub_id, "2e. subject_id khớp");
            TEST_ASSERT(ord && ord->valueint == 1, "2f. order_index khớp");
            TEST_ASSERT(name && strcmp(name->valuestring, "Chuong 1: Tong quan") == 0, "2g. name khớp");
            cJSON_Delete(root);
        }
        free(json_get);
    }

    /* 3. List Chapters lọc theo subject_id */
    char *json_list = NULL;
    rc = classification_chapter_list(db, sub_id, 10, 0, &json_list);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_list != NULL, "2h. classification_chapter_list(sub_id) thành công");
    if (json_list) {
        cJSON *root = cJSON_Parse(json_list);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(total && total->valueint == 2, "2i. Tổng số chapter của môn học = 2");
            cJSON_Delete(root);
        }
        free(json_list);
    }

    /* 4. Update Chapter */
    rc = classification_chapter_update(db, chap1_id, 10, "Chuong 1: Tong quan sua", "Mo ta sua");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "2j. classification_chapter_update() thành công");

    /* 5. Delete Chapter (chưa có lesson con) */
    rc = classification_chapter_delete(db, chap2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "2k. classification_chapter_delete() thành công");

    TEST_ASSERT(count_fk_violations(db) == 0, "2l. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 3: LESSON CRUD THÀNH CÔNG & PHÂN CẤP CHAPTER -> LESSON
 * ============================================================================
 */
static void test_group3_lesson_crud(void)
{
    printf("\n=== NHÓM 3: Lesson CRUD Thành công & Phân cấp ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub_id = 0, chap_id = 0;
    classification_subject_create(db, "OS", "He dieu hanh", NULL, &sub_id);
    classification_chapter_create(db, sub_id, 1, "Tien trinh", NULL, &chap_id);

    /* 1. Create Lesson */
    int les1_id = 0, les2_id = 0;
    int rc = classification_lesson_create(db, chap_id, 1, "Bai 1: Khai niem Process", "Mo ta", &les1_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && les1_id > 0, "3a. classification_lesson_create() les 1 thành công");

    rc = classification_lesson_create(db, chap_id, 2, "Bai 2: Tien trinh con & fork()", NULL, &les2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && les2_id > 0, "3b. classification_lesson_create() les 2 thành công");

    /* 2. Get Lesson */
    char *json_get = NULL;
    rc = classification_lesson_get(db, les1_id, &json_get);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_get != NULL, "3c. classification_lesson_get() thành công");
    if (json_get) {
        cJSON *root = cJSON_Parse(json_get);
        if (root) {
            cJSON *cid = cJSON_GetObjectItem(root, "chapter_id");
            cJSON *ord = cJSON_GetObjectItem(root, "order_index");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            TEST_ASSERT(cid && cid->valueint == chap_id, "3d. chapter_id khớp");
            TEST_ASSERT(ord && ord->valueint == 1, "3e. order_index khớp");
            TEST_ASSERT(name && strcmp(name->valuestring, "Bai 1: Khai niem Process") == 0, "3f. name khớp");
            cJSON_Delete(root);
        }
        free(json_get);
    }

    /* 3. List Lessons lọc theo chapter_id */
    char *json_list = NULL;
    rc = classification_lesson_list(db, chap_id, 10, 0, &json_list);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_list != NULL, "3g. classification_lesson_list(chap_id) thành công");
    if (json_list) {
        cJSON *root = cJSON_Parse(json_list);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(total && total->valueint == 2, "3h. Tổng số lesson = 2");
            cJSON_Delete(root);
        }
        free(json_list);
    }

    /* 4. Update Lesson */
    rc = classification_lesson_update(db, les1_id, 5, "Bai 1: Sua ten", "Sua mo ta");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "3i. classification_lesson_update() thành công");

    /* 5. Delete Lesson */
    rc = classification_lesson_delete(db, les2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "3j. classification_lesson_delete() thành công");

    TEST_ASSERT(count_fk_violations(db) == 0, "3k. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 4: TOPIC CRUD THÀNH CÔNG & PHÂN CẤP LESSON -> TOPIC
 * ============================================================================
 */
static void test_group4_topic_crud(void)
{
    printf("\n=== NHÓM 4: Topic CRUD Thành công & Phân cấp ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub_id = 0, chap_id = 0, les_id = 0;
    classification_subject_create(db, "DB", "Co so du lieu", NULL, &sub_id);
    classification_chapter_create(db, sub_id, 1, "SQL Can ban", NULL, &chap_id);
    classification_lesson_create(db, chap_id, 1, "Cau lenh SELECT", NULL, &les_id);

    /* 1. Create Topics */
    int top1_id = 0, top2_id = 0;
    int rc = classification_topic_create(db, les_id, "Menh de WHERE", "Dieu kien loc", &top1_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && top1_id > 0, "4a. classification_topic_create() top 1 thành công");

    rc = classification_topic_create(db, les_id, "ORDER BY & LIMIT", NULL, &top2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && top2_id > 0, "4b. classification_topic_create() top 2 thành công");

    /* 2. Get Topic */
    char *json_get = NULL;
    rc = classification_topic_get(db, top1_id, &json_get);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_get != NULL, "4c. classification_topic_get() thành công");
    if (json_get) {
        cJSON *root = cJSON_Parse(json_get);
        if (root) {
            cJSON *lid = cJSON_GetObjectItem(root, "lesson_id");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            TEST_ASSERT(lid && lid->valueint == les_id, "4d. lesson_id khớp");
            TEST_ASSERT(name && strcmp(name->valuestring, "Menh de WHERE") == 0, "4e. name khớp");
            cJSON_Delete(root);
        }
        free(json_get);
    }

    /* 3. List Topics lọc theo lesson_id */
    char *json_list = NULL;
    rc = classification_topic_list(db, les_id, 10, 0, &json_list);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json_list != NULL, "4f. classification_topic_list(les_id) thành công");
    if (json_list) {
        cJSON *root = cJSON_Parse(json_list);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(total && total->valueint == 2, "4g. Tổng số topics = 2");
            cJSON_Delete(root);
        }
        free(json_list);
    }

    /* 4. Update Topic */
    rc = classification_topic_update(db, top1_id, "Menh de WHERE nang cao", "Loc du lieu");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "4h. classification_topic_update() thành công");

    /* 5. Delete Topic */
    rc = classification_topic_delete(db, top2_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "4i. classification_topic_delete() thành công");

    TEST_ASSERT(count_fk_violations(db) == 0, "4j. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 5: KIỂM THỬ PARENT KHÔNG TỒN TẠI (NOT_FOUND)
 * ============================================================================
 */
static void test_group5_parent_not_found(void)
{
    printf("\n=== NHÓM 5: Kiểm thử Parent Không tồn tại (NOT_FOUND) ===\n");
    sqlite3 *db = create_in_memory_db();

    int out_id = 0;
    /* 1. Chapter với subject_id không tồn tại */
    int rc = classification_chapter_create(db, 9999, 1, "Chapter X", NULL, &out_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_NOT_FOUND, "5a. Chapter với subject_id=9999 trả về NOT_FOUND");
    TEST_ASSERT(out_id == 0, "5b. out_id được gán về 0 khi thất bại");

    /* 2. Lesson với chapter_id không tồn tại */
    rc = classification_lesson_create(db, 8888, 1, "Lesson X", NULL, &out_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_NOT_FOUND, "5c. Lesson với chapter_id=8888 trả về NOT_FOUND");

    /* 3. Topic với lesson_id không tồn tại */
    rc = classification_topic_create(db, 7777, "Topic X", NULL, &out_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_NOT_FOUND, "5d. Topic với lesson_id=7777 trả về NOT_FOUND");

    /* 4. Get/Update/Delete với ID không tồn tại */
    char *out_json = NULL;
    TEST_ASSERT(classification_subject_get(db, 9999, &out_json) == CLASSIFICATION_ERR_NOT_FOUND, "5e. subject_get(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_chapter_get(db, 9999, &out_json) == CLASSIFICATION_ERR_NOT_FOUND, "5f. chapter_get(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_lesson_get(db, 9999, &out_json) == CLASSIFICATION_ERR_NOT_FOUND, "5g. lesson_get(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_topic_get(db, 9999, &out_json) == CLASSIFICATION_ERR_NOT_FOUND, "5h. topic_get(9999) -> NOT_FOUND");

    TEST_ASSERT(classification_subject_update(db, 9999, "C", "N", NULL) == CLASSIFICATION_ERR_NOT_FOUND, "5i. subject_update(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_chapter_update(db, 9999, 1, "N", NULL) == CLASSIFICATION_ERR_NOT_FOUND, "5j. chapter_update(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_lesson_update(db, 9999, 1, "N", NULL) == CLASSIFICATION_ERR_NOT_FOUND, "5k. lesson_update(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_topic_update(db, 9999, "N", NULL) == CLASSIFICATION_ERR_NOT_FOUND, "5l. topic_update(9999) -> NOT_FOUND");

    TEST_ASSERT(classification_subject_delete(db, 9999) == CLASSIFICATION_ERR_NOT_FOUND, "5m. subject_delete(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_chapter_delete(db, 9999) == CLASSIFICATION_ERR_NOT_FOUND, "5n. chapter_delete(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_lesson_delete(db, 9999) == CLASSIFICATION_ERR_NOT_FOUND, "5o. lesson_delete(9999) -> NOT_FOUND");
    TEST_ASSERT(classification_topic_delete(db, 9999) == CLASSIFICATION_ERR_NOT_FOUND, "5p. topic_delete(9999) -> NOT_FOUND");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 6: VALIDATION THAM SỐ ĐẦU VÀO (INVALID_PARAM)
 * ============================================================================
 */
static void test_group6_param_validation(void)
{
    printf("\n=== NHÓM 6: Validation Tham số đầu vào (INVALID_PARAM) ===\n");
    sqlite3 *db = create_in_memory_db();
    int out_id = 0;
    char *out_json = NULL;

    /* 1. Con trỏ db == NULL */
    TEST_ASSERT(classification_subject_create(NULL, "C", "N", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6a. db == NULL -> INVALID_PARAM");
    TEST_ASSERT(classification_subject_list(NULL, 10, 0, &out_json) == CLASSIFICATION_ERR_INVALID_PARAM, "6b. list db == NULL -> INVALID_PARAM");

    /* 2. out pointer == NULL */
    TEST_ASSERT(classification_subject_create(db, "C", "N", NULL, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6c. out_id == NULL -> INVALID_PARAM");
    TEST_ASSERT(classification_subject_list(db, 10, 0, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6d. out_json == NULL -> INVALID_PARAM");

    /* 3. Dữ liệu rỗng / khoảng trắng */
    TEST_ASSERT(classification_subject_create(db, "", "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6e. code rỗng -> INVALID_PARAM");
    TEST_ASSERT(classification_subject_create(db, "   ", "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6f. code toàn whitespace -> INVALID_PARAM");
    TEST_ASSERT(classification_subject_create(db, "CODE", "", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6g. name rỗng -> INVALID_PARAM");
    TEST_ASSERT(classification_subject_create(db, "CODE", "  \t\n ", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6h. name toàn whitespace -> INVALID_PARAM");

    /* 4. ID hoặc order_index <= 0 */
    TEST_ASSERT(classification_chapter_create(db, 0, 1, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6i. subject_id = 0 -> INVALID_PARAM");
    TEST_ASSERT(classification_chapter_create(db, -1, 1, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6j. subject_id = -1 -> INVALID_PARAM");
    TEST_ASSERT(classification_chapter_create(db, 1, 0, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6k. order_index = 0 -> INVALID_PARAM");
    TEST_ASSERT(classification_chapter_create(db, 1, -5, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6l. order_index = -5 -> INVALID_PARAM");

    TEST_ASSERT(classification_lesson_create(db, 0, 1, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6m. chapter_id = 0 -> INVALID_PARAM");
    TEST_ASSERT(classification_lesson_create(db, 1, 0, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6n. lesson order_index = 0 -> INVALID_PARAM");

    TEST_ASSERT(classification_topic_create(db, 0, "Name", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6o. lesson_id = 0 -> INVALID_PARAM");
    TEST_ASSERT(classification_topic_create(db, 1, "", NULL, &out_id) == CLASSIFICATION_ERR_INVALID_PARAM, "6p. topic name rỗng -> INVALID_PARAM");

    /* 5. Update không cung cấp trường nào */
    TEST_ASSERT(classification_subject_update(db, 1, NULL, NULL, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6q. subject_update không có trường nào -> INVALID_PARAM");
    TEST_ASSERT(classification_chapter_update(db, 1, 0, NULL, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6r. chapter_update không có trường nào -> INVALID_PARAM");
    TEST_ASSERT(classification_lesson_update(db, 1, 0, NULL, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6s. lesson_update không có trường nào -> INVALID_PARAM");
    TEST_ASSERT(classification_topic_update(db, 1, NULL, NULL) == CLASSIFICATION_ERR_INVALID_PARAM, "6t. topic_update không có trường nào -> INVALID_PARAM");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 7: KIỂM THỬ RÀNG BUỘC UNIQUE (DUPLICATE)
 * ============================================================================
 */
static void test_group7_unique_constraints(void)
{
    printf("\n=== NHÓM 7: Ràng buộc UNIQUE (DUPLICATE) ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub1 = 0, sub2 = 0;
    classification_subject_create(db, "SUB1", "Mon 1", NULL, &sub1);
    classification_subject_create(db, "SUB2", "Mon 2", NULL, &sub2);

    /* 1. Trùng subject.code */
    int dup_sub = 0;
    int rc = classification_subject_create(db, "SUB1", "Mon 1 trung", NULL, &dup_sub);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7a. Trùng subject code -> CLASSIFICATION_ERR_DUPLICATE");

    /* 2. Trùng chapter.order_index trong cùng môn */
    int chap1 = 0, chap2 = 0, dup_chap = 0;
    classification_chapter_create(db, sub1, 1, "Chuong 1", NULL, &chap1);
    rc = classification_chapter_create(db, sub1, 1, "Chuong trung order", NULL, &dup_chap);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7b. Trùng chapter order_index trong cùng môn -> DUPLICATE");

    /* 3. Trùng chapter.name trong cùng môn */
    rc = classification_chapter_create(db, sub1, 2, "Chuong 1", NULL, &dup_chap);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7c. Trùng chapter name trong cùng môn -> DUPLICATE");

    /* 4. Cùng order_index và name nhưng ở môn khác nhau -> HỢP LỆ */
    rc = classification_chapter_create(db, sub2, 1, "Chuong 1", NULL, &chap2);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && chap2 > 0, "7d. Cùng order_index và name ở môn khác nhau -> HỢP LỆ");

    /* 5. Trùng lesson.order_index và name trong cùng chuong */
    int les1 = 0, dup_les = 0;
    classification_lesson_create(db, chap1, 1, "Bai 1", NULL, &les1);

    rc = classification_lesson_create(db, chap1, 1, "Bai khac", NULL, &dup_les);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7e. Trùng lesson order_index trong cùng chương -> DUPLICATE");

    rc = classification_lesson_create(db, chap1, 2, "Bai 1", NULL, &dup_les);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7f. Trùng lesson name trong cùng chương -> DUPLICATE");

    /* 6. Trùng topic.name trong cùng bài */
    int top1 = 0, dup_top = 0;
    classification_topic_create(db, les1, "Topic 1", NULL, &top1);

    rc = classification_topic_create(db, les1, "Topic 1", NULL, &dup_top);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7g. Trùng topic name trong cùng bài -> DUPLICATE");

    /* 7. Update sang giá trị trùng của bản ghi khác */
    int sub3 = 0;
    classification_subject_create(db, "SUB3", "Mon 3", NULL, &sub3);
    rc = classification_subject_update(db, sub3, "SUB1", NULL, NULL);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7h. Update subject sang code đã tồn tại -> DUPLICATE");

    int chap3 = 0;
    classification_chapter_create(db, sub1, 3, "Chuong 3", NULL, &chap3);
    rc = classification_chapter_update(db, chap3, 1, NULL, NULL);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7i. Update chapter sang order_index đã tồn tại trong môn -> DUPLICATE");

    rc = classification_chapter_update(db, chap3, 0, "Chuong 1", NULL);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_DUPLICATE, "7j. Update chapter sang name đã tồn tại trong môn -> DUPLICATE");

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 8: UPDATE KHÔNG LÀM MẤT CÁC TRƯỜNG KHÔNG CẬP NHẬT
 * ============================================================================
 */
static void test_group8_partial_updates(void)
{
    printf("\n=== NHÓM 8: Update không làm mất các trường không cập nhật ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub_id = 0, chap_id = 0, les_id = 0, top_id = 0;
    classification_subject_create(db, "MTH101", "Giai tich 1", "Mo ta mon toan", &sub_id);
    classification_chapter_create(db, sub_id, 1, "Dao ham", "Mo ta chuong dao ham", &chap_id);
    classification_lesson_create(db, chap_id, 1, "Dao ham cap 1", "Mo ta bai dao ham", &les_id);
    classification_topic_create(db, les_id, "Quy tac dao ham", "Mo ta topic quy tac", &top_id);

    /* 1. Subject: chỉ sửa name, code và description phải giữ nguyên */
    int rc = classification_subject_update(db, sub_id, NULL, "Giai tich 1 (Dieu chinh)", NULL);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "8a. Update subject name thành công");
    char *json = NULL;
    classification_subject_get(db, sub_id, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *code = cJSON_GetObjectItem(root, "code");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            TEST_ASSERT(code && strcmp(code->valuestring, "MTH101") == 0, "8b. Code MTH101 bảo toàn");
            TEST_ASSERT(name && strcmp(name->valuestring, "Giai tich 1 (Dieu chinh)") == 0, "8c. Name đã đổi");
            TEST_ASSERT(desc && strcmp(desc->valuestring, "Mo ta mon toan") == 0, "8d. Description bảo toàn");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Chapter: chỉ sửa order_index, name và description phải giữ nguyên */
    rc = classification_chapter_update(db, chap_id, 99, NULL, NULL);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "8e. Update chapter order_index thành công");
    json = NULL;
    classification_chapter_get(db, chap_id, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *ord = cJSON_GetObjectItem(root, "order_index");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            TEST_ASSERT(ord && ord->valueint == 99, "8f. order_index đổi sang 99");
            TEST_ASSERT(name && strcmp(name->valuestring, "Dao ham") == 0, "8g. name Dao ham bảo toàn");
            TEST_ASSERT(desc && strcmp(desc->valuestring, "Mo ta chuong dao ham") == 0, "8h. description bảo toàn");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 3. Lesson: chỉ sửa description, order_index và name bảo toàn */
    rc = classification_lesson_update(db, les_id, 0, NULL, "Mo ta bai moi");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "8i. Update lesson description thành công");
    json = NULL;
    classification_lesson_get(db, les_id, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *ord = cJSON_GetObjectItem(root, "order_index");
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            TEST_ASSERT(ord && ord->valueint == 1, "8j. order_index bảo toàn");
            TEST_ASSERT(name && strcmp(name->valuestring, "Dao ham cap 1") == 0, "8k. name bảo toàn");
            TEST_ASSERT(desc && strcmp(desc->valuestring, "Mo ta bai moi") == 0, "8l. description đã đổi");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 4. Topic: chỉ sửa description, name bảo toàn */
    rc = classification_topic_update(db, top_id, NULL, "Mo ta topic moi");
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "8m. Update topic description thành công");
    json = NULL;
    classification_topic_get(db, top_id, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *name = cJSON_GetObjectItem(root, "name");
            cJSON *desc = cJSON_GetObjectItem(root, "description");
            TEST_ASSERT(name && strcmp(name->valuestring, "Quy tac dao ham") == 0, "8n. name bảo toàn");
            TEST_ASSERT(desc && strcmp(desc->valuestring, "Mo ta topic moi") == 0, "8o. description đã đổi");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 9: ON DELETE RESTRICT & BẢO VỆ TOÀN VẸN CÂY PHÂN CẤP
 * ============================================================================
 */
static void test_group9_delete_restrictions(void)
{
    printf("\n=== NHÓM 9: ON DELETE RESTRICT & Toàn vẹn Tham chiếu ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub_id = 0, chap_id = 0, les_id = 0, top_id = 0;
    classification_subject_create(db, "PHY", "Vat ly Dai cuong", NULL, &sub_id);
    classification_chapter_create(db, sub_id, 1, "Co hoc", NULL, &chap_id);
    classification_lesson_create(db, chap_id, 1, "Dong luc hoc", NULL, &les_id);
    classification_topic_create(db, les_id, "Dinh luat Newton", NULL, &top_id);

    /* 1. Xóa Subject khi còn Chapter -> Bị chặn (IN_USE) */
    int rc = classification_subject_delete(db, sub_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_IN_USE, "9a. Chặn xóa subject khi còn chapter con -> IN_USE");

    /* 2. Xóa Chapter khi còn Lesson -> Bị chặn (IN_USE) */
    rc = classification_chapter_delete(db, chap_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_IN_USE, "9b. Chặn xóa chapter khi còn lesson con -> IN_USE");

    /* 3. Xóa Lesson khi còn Topic -> Bị chặn (IN_USE) */
    rc = classification_lesson_delete(db, les_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_IN_USE, "9c. Chặn xóa lesson khi còn topic con -> IN_USE");

    /* 4. Gắn câu hỏi vào Topic (questions.topic_id = top_id) */
    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO questions (content, topic, difficulty, topic_id) "
             "VALUES ('Phat bieu dinh luat II Newton?', 'Physics', 'EASY', %d);", top_id);
    char *err = NULL;
    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    TEST_ASSERT(rc == SQLITE_OK, "9d. Thêm question liên kết topic_id thành công");

    /* 5. Xóa Topic khi có Question tham chiếu -> Bị chặn (IN_USE) */
    rc = classification_topic_delete(db, top_id);
    TEST_ASSERT(rc == CLASSIFICATION_ERR_IN_USE, "9e. Chặn xóa topic khi có question tham chiếu -> IN_USE");

    /* 6. Gỡ bỏ tham chiếu từ question (topic_id = NULL) */
    sqlite3_exec(db, "UPDATE questions SET topic_id = NULL;", NULL, NULL, NULL);

    /* 7. Xóa thành công Topic khi không còn tham chiếu */
    rc = classification_topic_delete(db, top_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "9f. Xóa topic thành công sau khi gỡ tham chiếu question");

    /* 8. Xóa thành công Lesson sau khi Topic đã xóa */
    rc = classification_lesson_delete(db, les_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "9g. Xóa lesson thành công khi không còn topic");

    /* 9. Xóa thành công Chapter sau khi Lesson đã xóa */
    rc = classification_chapter_delete(db, chap_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "9h. Xóa chapter thành công khi không còn lesson");

    /* 10. Xóa thành công Subject sau khi Chapter đã xóa */
    rc = classification_subject_delete(db, sub_id);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS, "9i. Xóa subject thành công khi không còn chapter");

    /* Kiểm tra foreign keys toàn hệ thống */
    TEST_ASSERT(count_fk_violations(db) == 0, "9j. PRAGMA foreign_key_check = 0 vi phạm");
    sqlite3_close(db);
}

/*
 * ============================================================================
 * NHÓM 10: LIST, FILTER VÀ PAGINATION
 * ============================================================================
 */
static void test_group10_filter_and_pagination(void)
{
    printf("\n=== NHÓM 10: List, Filter & Pagination ===\n");
    sqlite3 *db = create_in_memory_db();

    int sub1 = 0, sub2 = 0;
    classification_subject_create(db, "S1", "Subject 1", NULL, &sub1);
    classification_subject_create(db, "S2", "Subject 2", NULL, &sub2);

    int c1 = 0, c2 = 0, c3 = 0, c4 = 0;
    classification_chapter_create(db, sub1, 1, "S1 Chap 1", NULL, &c1);
    classification_chapter_create(db, sub1, 2, "S1 Chap 2", NULL, &c2);
    classification_chapter_create(db, sub1, 3, "S1 Chap 3", NULL, &c3);
    classification_chapter_create(db, sub2, 1, "S2 Chap 1", NULL, &c4);

    /* 1. Lọc chapter theo subject_id */
    char *json = NULL;
    int rc = classification_chapter_list(db, sub1, 10, 0, &json);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json != NULL, "10a. Chapter list sub1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            cJSON *count = cJSON_GetObjectItem(root, "count");
            TEST_ASSERT(total && total->valueint == 3, "10b. sub1 có 3 chapters");
            TEST_ASSERT(count && count->valueint == 3, "10c. count = 3");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 2. Lọc chapter theo sub2 */
    json = NULL;
    rc = classification_chapter_list(db, sub2, 10, 0, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            TEST_ASSERT(total && total->valueint == 1, "10d. sub2 có 1 chapter");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 3. Phân trang: limit = 2, offset = 1 cho sub1 */
    json = NULL;
    rc = classification_chapter_list(db, sub1, 2, 1, &json);
    TEST_ASSERT(rc == CLASSIFICATION_SUCCESS && json != NULL, "10e. Phân trang limit=2, offset=1 thành công");
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            cJSON *count = cJSON_GetObjectItem(root, "count");
            cJSON *limit = cJSON_GetObjectItem(root, "limit");
            cJSON *offset = cJSON_GetObjectItem(root, "offset");
            cJSON *arr = cJSON_GetObjectItem(root, "chapters");
            TEST_ASSERT(total && total->valueint == 3, "10f. Total = 3");
            TEST_ASSERT(count && count->valueint == 2, "10g. Count = 2");
            TEST_ASSERT(limit && limit->valueint == 2, "10h. Limit = 2");
            TEST_ASSERT(offset && offset->valueint == 1, "10i. Offset = 1");
            TEST_ASSERT(arr && cJSON_GetArraySize(arr) == 2, "10j. Trả về đúng 2 phần tử");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 4. Offset vượt quá tổng số bản ghi */
    json = NULL;
    rc = classification_chapter_list(db, sub1, 10, 100, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *total = cJSON_GetObjectItem(root, "total");
            cJSON *count = cJSON_GetObjectItem(root, "count");
            TEST_ASSERT(total && total->valueint == 3, "10k. Total vẫn bằng 3");
            TEST_ASSERT(count && count->valueint == 0, "10l. Count = 0 khi offset vượt quá");
            cJSON_Delete(root);
        }
        free(json);
    }

    /* 5. Giá trị limit / offset mặc định an toàn */
    json = NULL;
    rc = classification_chapter_list(db, sub1, -1, -5, &json);
    if (json) {
        cJSON *root = cJSON_Parse(json);
        if (root) {
            cJSON *limit = cJSON_GetObjectItem(root, "limit");
            cJSON *offset = cJSON_GetObjectItem(root, "offset");
            TEST_ASSERT(limit && limit->valueint == 20, "10m. Limit <= 0 mặc định thành 20");
            TEST_ASSERT(offset && offset->valueint == 0, "10n. Offset < 0 mặc định thành 0");
            cJSON_Delete(root);
        }
        free(json);
    }

    sqlite3_close(db);
}

int main(void)
{
    printf("=================================================================\n");
    printf("  BỘ KIỂM THỬ: CLASSIFICATION CRUD APIS (TASK 06.4)\n");
    printf("=================================================================\n");

    test_group1_subject_crud();
    test_group2_chapter_crud();
    test_group3_lesson_crud();
    test_group4_topic_crud();
    test_group5_parent_not_found();
    test_group6_param_validation();
    test_group7_unique_constraints();
    test_group8_partial_updates();
    test_group9_delete_restrictions();
    test_group10_filter_and_pagination();

    printf("\n=================================================================\n");
    printf("  KẾT QUẢ KIỂM THỬ: %d / %d PASS\n", g_tests_passed, g_tests_run);
    printf("=================================================================\n");

    return (g_tests_passed == g_tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}

