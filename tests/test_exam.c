/*
 * test_exam.c - Kiem thu module Exam tren database tam trong bo nho.
 *
 * Bien dich va chay tu thu muc goc:
 *   gcc -std=c11 -Wall -Wextra -Iserver/include tests/test_exam.c \
 *       server/src/exam/exam.c -o tests/test_exam -lsqlite3 -lcjson
 *   ./tests/test_exam database/schema.sql
 */
#include "exam.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, name) do { \
    if (cond) { g_pass++; printf("  [OK]   %s\n", name); } \
    else { g_fail++; printf("  [FAIL] %s (dong %d)\n", name, __LINE__); } \
} while (0)

static void exec_sql(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) {
        fprintf(stderr, "SQL loi: %s\n%s\n", err, sql);
        sqlite3_free(err);
        exit(2);
    }
}

static int query_int(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st = NULL;
    int v = -999;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) == SQLITE_OK && sqlite3_step(st) == SQLITE_ROW) {
        v = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (buf != NULL && fread(buf, 1, (size_t)n, f) == (size_t)n) buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Dap an dung / sai cua mot cau hoi (dap an dung la dap an co id nho nhat) */
static int answer_of(sqlite3 *db, int qid, int correct)
{
    char sql[160];
    snprintf(sql, sizeof(sql),
             "SELECT id FROM answers WHERE question_id = %d AND is_correct = %d ORDER BY id LIMIT 1",
             qid, correct);
    return query_int(db, sql);
}

/* Cau hoi thu pos trong bo cau hoi cua phong */
static int room_q(sqlite3 *db, int room_id, int pos)
{
    char sql[128];
    snprintf(sql, sizeof(sql),
             "SELECT question_id FROM room_questions WHERE room_id = %d AND position = %d",
             room_id, pos);
    return query_int(db, sql);
}

static double json_num(const char *json, const char *key)
{
    cJSON *r = cJSON_Parse(json);
    cJSON *v = cJSON_GetObjectItem(r, key);
    double d = cJSON_IsNumber(v) ? v->valuedouble : -1;
    cJSON_Delete(r);
    return d;
}

static void seed(sqlite3 *db)
{
    exec_sql(db,
        "INSERT INTO users (id, username, password_hash) VALUES "
        " (1, 'minh', 'x'), (2, 'owner', 'x'), (3, 'nguoila', 'x'), (5, 'an', 'x');"
        "INSERT INTO users (id, username, password_hash, role) VALUES (4, 'admin', 'x', 'ADMIN');"
        "INSERT INTO rooms (id, owner_id, name, question_count, duration_minutes, status, end_time) VALUES "
        " (1, 2, 'Phong dang thi', 4, 30, 'IN_PROGRESS', NULL),"
        " (2, 2, 'Phong dang cho', 4, 30, 'WAITING', NULL),"
        " (3, 2, 'Phong het gio', 4, 30, 'IN_PROGRESS', '2000-01-01T00:00:00Z'),"
        " (4, 2, 'Phong chua co de', 4, 30, 'IN_PROGRESS', NULL);"
        "INSERT INTO room_members (room_id, user_id) VALUES (1, 1), (2, 1), (3, 1), (1, 5), (4, 1);");

    /* 6 cau hoi, moi cau 4 dap an, dap an dau tien la dung */
    for (int q = 1; q <= 6; q++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "INSERT INTO questions (id, content, topic, difficulty) VALUES (%d, 'Cau %d', 'Mang', 'EASY');"
            "INSERT INTO answers (question_id, content, is_correct) VALUES "
            " (%d, 'A', 1), (%d, 'B', 0), (%d, 'C', 0), (%d, 'D', 0);",
            q, q, q, q, q, q);
        exec_sql(db, sql);
    }

    /* Bo de cua phong do phan Room (Tuan Anh) thiet lap; o day gia lap: phong 1, 3 dung cau 1-4 */
    exec_sql(db,
        "INSERT INTO room_questions (room_id, question_id, position) VALUES "
        " (1, 1, 1), (1, 2, 2), (1, 3, 3), (1, 4, 4),"
        " (3, 1, 1), (3, 2, 2), (3, 3, 3), (3, 4, 4);");
}

static const char *json_str(cJSON *o, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static void test_review_and_room_result(sqlite3 *db, int a1)
{
    printf("== EXAM_REVIEW (xem lai bai) ==\n");
    CHECK(exam_review(db, a1, 1) == NULL && exam_last_error() == EXAM_ERR_ROOM_NOT_FINISHED,
          "thi sinh chua xem dap an khi phong chua ket thuc");
    CHECK(exam_review(db, a1, 3) == NULL && exam_last_error() == EXAM_ERR_ATTEMPT_NOT_FOUND,
          "nguoi ngoai khong xem duoc bai");

    char *rv = exam_review(db, a1, 2);
    CHECK(rv != NULL, "chu phong (giao vien) xem duoc bai bat ky luc nao");
    if (rv != NULL) {
        cJSON *r = cJSON_Parse(rv);
        cJSON *qs = cJSON_GetObjectItem(r, "questions");
        CHECK(cJSON_GetArraySize(qs) == 4, "co du 4 cau trong bai xem lai");
        CHECK(strcmp(json_str(cJSON_GetArrayItem(qs, 0), "result"), "CORRECT") == 0, "cau 1 dung (da doi dap an)");
        CHECK(strcmp(json_str(cJSON_GetArrayItem(qs, 2), "result"), "WRONG") == 0, "cau 3 sai");
        CHECK(strcmp(json_str(cJSON_GetArrayItem(qs, 3), "result"), "UNANSWERED") == 0, "cau 4 bo trong");
        cJSON *q3 = cJSON_GetArrayItem(qs, 2);
        int qid3 = cJSON_GetObjectItem(q3, "question_id")->valueint;
        CHECK(cJSON_GetObjectItem(q3, "correct_answer_id")->valueint == answer_of(db, qid3, 1), "hien dung dap an dung");
        CHECK(cJSON_IsNull(cJSON_GetObjectItem(cJSON_GetArrayItem(qs, 3), "selected_answer_id")), "cau bo trong selected = null");
        CHECK(strcmp(json_str(r, "username"), "minh") == 0, "co ten thi sinh");
        cJSON_Delete(r);
        free(rv);
    }
    rv = exam_review(db, a1, 4);
    CHECK(rv != NULL, "ADMIN xem duoc bai");
    free(rv);

    printf("== ROOM_RESULT (ket qua thi cua phong) ==\n");
    CHECK(exam_room_result(db, 1, 3) == NULL && exam_last_error() == EXAM_ERR_FORBIDDEN, "nguoi ngoai phong bi tu choi");
    CHECK(exam_room_result(db, 1, 1) == NULL && exam_last_error() == EXAM_ERR_FORBIDDEN,
          "thi sinh khong xem ket qua ca phong");

    char *rr = exam_room_result(db, 1, 2);
    CHECK(rr != NULL, "giao vien xem ket qua phong");
    if (rr != NULL) {
        printf("         %.300s...\n", rr);
        cJSON *r = cJSON_Parse(rr);
        CHECK(cJSON_GetObjectItem(r, "member_count")->valueint == 2, "lop co 2 thi sinh (khong tinh giao vien)");
        CHECK(cJSON_GetObjectItem(r, "submitted_count")->valueint == 1, "1 ban da thi");
        CHECK(cJSON_GetObjectItem(r, "not_started_count")->valueint == 1, "1 ban chua thi");
        cJSON *res = cJSON_GetObjectItem(r, "results");
        CHECK(cJSON_GetArraySize(res) == 1, "moi thi sinh chi 1 dong ket qua");
        CHECK(cJSON_GetObjectItem(cJSON_GetArrayItem(res, 0), "score")->valuedouble == 5.0,
              "lay luot thi diem cao nhat (5.00 thay vi 2.50, 0.00)");
        CHECK(cJSON_GetObjectItem(cJSON_GetArrayItem(res, 0), "rank") == NULL, "khong co xep hang");
        cJSON *ns = cJSON_GetObjectItem(r, "not_started");
        CHECK(cJSON_GetArraySize(ns) == 1 && strcmp(json_str(cJSON_GetArrayItem(ns, 0), "username"), "an") == 0,
              "danh sach chua thi co ban 'an'");
        cJSON_Delete(r);
        free(rr);
    }

    exec_sql(db, "UPDATE rooms SET status = 'FINISHED' WHERE id = 1");
    rv = exam_review(db, a1, 1);
    CHECK(rv != NULL, "phong ket thuc: thi sinh xem lai bai cua minh");
    free(rv);
    rr = exam_room_result(db, 1, 4);
    CHECK(rr != NULL, "ADMIN xem ket qua phong");
    free(rr);
}

int main(int argc, char **argv)
{
    const char *schema_path = argc > 1 ? argv[1] : "database/schema.sql";
    char *schema = read_file(schema_path);
    if (schema == NULL) { fprintf(stderr, "Khong doc duoc %s\n", schema_path); return 2; }

    sqlite3 *db = NULL;
    sqlite3_open(":memory:", &db);
    exec_sql(db, schema);
    free(schema);
    seed(db);

    printf("== EXAM_START ==\n");
    CHECK(exam_start(db, 0, 1) == EXAM_ERR_INVALID_PAYLOAD, "room_id khong hop le");
    CHECK(exam_start(db, 99, 1) == EXAM_ERR_ROOM_NOT_FOUND, "phong khong ton tai");
    CHECK(exam_start(db, 1, 3) == EXAM_ERR_NOT_ROOM_MEMBER, "chua tham gia phong");
    CHECK(exam_start(db, 2, 1) == EXAM_ERR_EXAM_NOT_AVAILABLE, "phong WAITING chua cho thi");
    CHECK(exam_start(db, 3, 1) == EXAM_ERR_EXAM_NOT_AVAILABLE, "phong da qua end_time");
    CHECK(exam_start(db, 4, 1) == EXAM_ERR_EXAM_NOT_AVAILABLE, "phong chua co bo de thi chua cho thi");

    int a1 = exam_start(db, 1, 1);
    CHECK(a1 > 0, "bat dau thi thanh cong");
    CHECK(exam_start(db, 1, 1) == EXAM_ERR_ATTEMPT_ALREADY_STARTED, "khong mo 2 luot cung luc");

    char *data = exam_start_data(db, a1, 1);
    CHECK(data != NULL, "tao data EXAM_START_RESULT");
    if (data != NULL) {
        cJSON *r = cJSON_Parse(data);
        CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(r, "questions")) == 4, "tra ve 4 cau hoi");
        cJSON *q0 = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "questions"), 0);
        CHECK(cJSON_GetArraySize(cJSON_GetObjectItem(q0, "answers")) == 4, "moi cau 4 dap an");
        CHECK(strstr(data, "is_correct") == NULL, "khong lo is_correct");
        CHECK(cJSON_IsString(cJSON_GetObjectItem(r, "deadline")), "co deadline");
        cJSON_Delete(r);
        free(data);
    }
    CHECK(exam_start_data(db, a1, 3) == NULL && exam_last_error() == EXAM_ERR_ATTEMPT_NOT_FOUND,
          "nguoi khac khong xem duoc de");

    printf("== EXAM_ANSWER ==\n");
    int q1 = room_q(db, 1, 1), q2 = room_q(db, 1, 2), q3 = room_q(db, 1, 3);
    int outside = query_int(db,
        "SELECT id FROM questions WHERE id NOT IN (SELECT question_id FROM room_questions WHERE room_id = 1) LIMIT 1");
    CHECK(exam_answer(db, a1, 3, q1, answer_of(db, q1, 1)) == EXAM_ERR_ATTEMPT_NOT_FOUND, "khong sua bai nguoi khac");
    CHECK(exam_answer(db, a1, 1, outside, answer_of(db, outside, 1)) == EXAM_ERR_QUESTION_NOT_IN_EXAM,
          "cau hoi ngoai de");
    CHECK(exam_answer(db, a1, 1, q1, answer_of(db, q2, 1)) == EXAM_ERR_ANSWER_NOT_IN_QUESTION,
          "dap an cua cau khac");
    CHECK(exam_answer(db, a1, 1, q1, 0) == EXAM_ERR_INVALID_PAYLOAD, "answer_id khong hop le");

    CHECK(exam_answer(db, a1, 1, q1, answer_of(db, q1, 0)) == EXAM_OK, "chon dap an sai cau 1");
    CHECK(exam_answer(db, a1, 1, q1, answer_of(db, q1, 1)) == EXAM_OK, "doi sang dap an dung cau 1");
    char sql[160];
    snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM attempt_answers WHERE attempt_id = %d AND question_id = %d", a1, q1);
    CHECK(query_int(db, sql) == 1, "doi dap an chi cap nhat, khong tao ban ghi thu 2");
    CHECK(exam_answer(db, a1, 1, q2, answer_of(db, q2, 1)) == EXAM_OK, "cau 2 dung");
    CHECK(exam_answer(db, a1, 1, q3, answer_of(db, q3, 0)) == EXAM_OK, "cau 3 sai, cau 4 bo trong");

    printf("== EXAM_RESULT truoc khi nop ==\n");
    CHECK(exam_result(db, a1, 1) == NULL && exam_last_error() == EXAM_ERR_NOT_SUBMITTED, "chua nop thi chua co ket qua");

    printf("== EXAM_SUBMIT ==\n");
    CHECK(exam_submit(db, a1, 3) == EXAM_ERR_ATTEMPT_NOT_FOUND, "khong nop ho nguoi khac");
    CHECK(exam_submit(db, a1, 1) == EXAM_OK, "nop bai");
    CHECK(exam_submit(db, a1, 1) == EXAM_ERR_ALREADY_SUBMITTED, "khong nop 2 lan");
    CHECK(exam_answer(db, a1, 1, q3, answer_of(db, q3, 1)) == EXAM_ERR_ALREADY_SUBMITTED, "khong sua sau khi nop");

    char *res = exam_result(db, a1, 1);
    CHECK(res != NULL, "lay ket qua");
    if (res != NULL) {
        printf("         %s\n", res);
        CHECK(json_num(res, "total_questions") == 4, "tong 4 cau");
        CHECK(json_num(res, "correct_count") == 2, "dung 2 cau");
        CHECK(json_num(res, "score") == 5.0, "diem = 2 * 10 / 4 = 5.00");
        free(res);
    }
    CHECK(exam_result(db, a1, 3) == NULL && exam_last_error() == EXAM_ERR_ATTEMPT_NOT_FOUND, "khong xem ket qua nguoi khac");

    printf("== Het gio ==\n");
    int a2 = exam_start(db, 1, 1);
    CHECK(a2 > 0, "thi lai sau khi da nop");
    CHECK(exam_answer(db, a2, 1, q1, answer_of(db, q1, 1)) == EXAM_OK, "tra loi dung 1 cau");
    snprintf(sql, sizeof(sql),
             "UPDATE exam_attempts SET started_at = strftime('%%Y-%%m-%%dT%%H:%%M:%%SZ', 'now', '-31 minutes') WHERE id = %d", a2);
    exec_sql(db, sql);
    CHECK(exam_answer(db, a2, 1, q2, answer_of(db, q2, 1)) == EXAM_ERR_TIME_EXPIRED, "het gio khong luu dap an");
    res = exam_result(db, a2, 1);
    CHECK(res != NULL && json_num(res, "score") == 2.5, "server tu cham khi het gio (1/4 = 2.50)");
    free(res);

    int a3 = exam_start(db, 1, 1);
    snprintf(sql, sizeof(sql),
             "UPDATE exam_attempts SET started_at = strftime('%%Y-%%m-%%dT%%H:%%M:%%SZ', 'now', '-2 hours') WHERE id = %d", a3);
    exec_sql(db, sql);
    CHECK(exam_finalize_expired(db) == 1, "exam_finalize_expired cham 1 bai het gio");
    CHECK(exam_submit(db, a3, 1) == EXAM_ERR_ALREADY_SUBMITTED, "bai da tu cham thi khong nop lai");

    printf("== EXAM_HISTORY ==\n");
    char *hist = exam_history(db, 1);
    CHECK(hist != NULL, "lay lich su");
    if (hist != NULL) {
        cJSON *r = cJSON_Parse(hist);
        cJSON *list = cJSON_GetObjectItem(r, "attempts");
        CHECK(cJSON_GetArraySize(list) == 3, "co 3 luot thi da hoan thanh");
        cJSON *first = cJSON_GetArrayItem(list, 0);
        CHECK(cJSON_GetObjectItem(first, "attempt_id")->valueint == a1, "luot moi nhat dung dau");
        CHECK(strcmp(cJSON_GetObjectItem(first, "room_name")->valuestring, "Phong dang thi") == 0, "co room_name");
        cJSON_Delete(r);
        free(hist);
    }
    hist = exam_history(db, 3);
    CHECK(hist != NULL && strcmp(hist, "{\"attempts\":[]}") == 0, "chua thi thi tra ve mang rong");
    free(hist);

    CHECK(strcmp(exam_error_code(EXAM_ERR_TIME_EXPIRED), "EXAM_TIME_EXPIRED") == 0, "ma loi dung ten protocol");

    test_review_and_room_result(db, a1);

    sqlite3_close(db);
    printf("\nKet qua: %d dat, %d loi\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
