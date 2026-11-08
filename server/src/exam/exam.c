/*
 * exam.c - Nghiep vu lam bai thi, cham diem, ket qua va lich su (Cong Minh)
 *
 * Quy tac (docs/protocol.md muc 11):
 * - Chi thanh vien cua phong moi duoc thi, phong phai dang IN_PROGRESS va da co bo de
 *   (bo de do phan Room cua Tuan Anh thiet lap trong room_questions).
 * - Moi lan chon/doi dap an deu luu ngay (UPSERT vao attempt_answers).
 * - Han nop = started_at + duration_minutes (hoac end_time cua phong neu som hon).
 * - Server tu cham khi het gio, khong phu thuoc client.
 * - Diem = so cau dung * 10 / tong so cau, lam tron 2 chu so.
 * - Khong bao gio tra is_correct cho client.
 */
#define _POSIX_C_SOURCE 200809L

#include "exam.h"

#include <cjson/cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- Tien ich chung ---------- */

static int g_last_error = EXAM_OK;

static int set_err(int err)
{
    g_last_error = err;
    return err;
}

int exam_last_error(void)
{
    return g_last_error;
}

static long long now_epoch(void)
{
    return (long long)time(NULL);
}

/* Dinh dang ISO 8601 UTC: 2026-10-26T13:00:00Z */
static void format_iso(long long epoch, char *buf, size_t size)
{
    time_t t = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&t, &tm_utc);
    strftime(buf, size, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static double round2(double x)
{
    return (double)(long long)(x * 100.0 + 0.5) / 100.0;
}

/* Tra ve 1 neu cau truy van co it nhat 1 dong, 0 neu khong, am neu loi */
static int exists_2int(sqlite3 *db, const char *sql, int a, int b)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return EXAM_ERR_DATABASE;
    sqlite3_bind_int(st, 1, a);
    sqlite3_bind_int(st, 2, b);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc == SQLITE_ROW) return 1;
    if (rc == SQLITE_DONE) return 0;
    return EXAM_ERR_DATABASE;
}

/* Tra ve gia tri COUNT(*) (>= 0) hoac ma loi */
static int count_2int(sqlite3 *db, const char *sql, int a, int b)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return EXAM_ERR_DATABASE;
    sqlite3_bind_int(st, 1, a);
    sqlite3_bind_int(st, 2, b);
    int result = EXAM_ERR_DATABASE;
    if (sqlite3_step(st) == SQLITE_ROW) result = sqlite3_column_int(st, 0);
    sqlite3_finalize(st);
    return result;
}

/* ---------- Thong tin luot thi ---------- */

typedef struct {
    int attempt_id;
    int room_id;
    int user_id;
    int duration_minutes;
    long long started_epoch;
    long long deadline_epoch;
    int submitted;               /* 1 neu da nop / da cham */
    long long submitted_epoch;
    double score;
} AttemptInfo;

/*
 * Doc luot thi mode EXAM. user_id = 0 nghia la khong kiem tra chu so huu
 * (chi dung noi bo khi tu dong cham bai het gio).
 */
static int load_attempt(sqlite3 *db, int attempt_id, int user_id, AttemptInfo *out)
{
    static const char *sql =
        "SELECT a.id, a.room_id, a.user_id, r.duration_minutes, "
        "       CAST(strftime('%s', a.started_at) AS INTEGER), "
        "       CAST(strftime('%s', r.end_time) AS INTEGER), "
        "       CAST(strftime('%s', a.submitted_at) AS INTEGER), "
        "       a.score "
        "FROM exam_attempts a JOIN rooms r ON r.id = a.room_id "
        "WHERE a.id = ?1 AND a.mode = 'EXAM' AND (?2 = 0 OR a.user_id = ?2)";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK) return EXAM_ERR_DATABASE;
    sqlite3_bind_int(st, 1, attempt_id);
    sqlite3_bind_int(st, 2, user_id);

    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return rc == SQLITE_DONE ? EXAM_ERR_ATTEMPT_NOT_FOUND : EXAM_ERR_DATABASE;
    }

    memset(out, 0, sizeof(*out));
    out->attempt_id = sqlite3_column_int(st, 0);
    out->room_id = sqlite3_column_int(st, 1);
    out->user_id = sqlite3_column_int(st, 2);
    out->duration_minutes = sqlite3_column_int(st, 3);
    out->started_epoch = sqlite3_column_int64(st, 4);
    out->deadline_epoch = out->started_epoch + (long long)out->duration_minutes * 60;

    /* Neu phong co end_time som hon thi han nop la end_time */
    if (sqlite3_column_type(st, 5) != SQLITE_NULL) {
        long long end_epoch = sqlite3_column_int64(st, 5);
        if (end_epoch < out->deadline_epoch) out->deadline_epoch = end_epoch;
    }

    if (sqlite3_column_type(st, 6) != SQLITE_NULL) {
        out->submitted = 1;
        out->submitted_epoch = sqlite3_column_int64(st, 6);
        out->score = sqlite3_column_double(st, 7);
    }

    sqlite3_finalize(st);
    return EXAM_OK;
}

static int count_total(sqlite3 *db, int room_id)
{
    return count_2int(db,
        "SELECT COUNT(*) FROM room_questions WHERE room_id = ?1 AND ?2 = ?2",
        room_id, 0);
}

/* So cau tra loi dung, chi tinh cac cau thuoc bo cau hoi cua phong */
static int count_correct(sqlite3 *db, int attempt_id, int room_id)
{
    return count_2int(db,
        "SELECT COUNT(*) FROM attempt_answers aa "
        "JOIN answers an ON an.id = aa.answer_id AND an.question_id = aa.question_id "
        "JOIN room_questions rq ON rq.question_id = aa.question_id AND rq.room_id = ?2 "
        "WHERE aa.attempt_id = ?1 AND an.is_correct = 1",
        attempt_id, room_id);
}

/* Cham diem va danh dau da nop. Chi cap nhat neu chua nop (tranh cham 2 lan) */
static int grade_attempt(sqlite3 *db, AttemptInfo *a, long long submitted_epoch)
{
    int total = count_total(db, a->room_id);
    int correct = count_correct(db, a->attempt_id, a->room_id);
    if (total < 0 || correct < 0) return EXAM_ERR_DATABASE;

    double score = total > 0 ? round2(correct * 10.0 / total) : 0.0;
    char submitted_at[32];
    format_iso(submitted_epoch, submitted_at, sizeof(submitted_at));

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "UPDATE exam_attempts SET submitted_at = ?1, score = ?2 "
            "WHERE id = ?3 AND submitted_at IS NULL",
            -1, &st, NULL) != SQLITE_OK) {
        return EXAM_ERR_DATABASE;
    }
    sqlite3_bind_text(st, 1, submitted_at, -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(st, 2, score);
    sqlite3_bind_int(st, 3, a->attempt_id);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return EXAM_ERR_DATABASE;
    if (sqlite3_changes(db) == 0) return EXAM_ERR_ALREADY_SUBMITTED;

    a->submitted = 1;
    a->submitted_epoch = submitted_epoch;
    a->score = score;
    return EXAM_OK;
}

/* Neu da het gio ma chua nop: cham voi thoi diem nop = han nop. Tra ve 1 neu vua cham */
static int finalize_if_expired(sqlite3 *db, AttemptInfo *a)
{
    if (a->submitted || now_epoch() < a->deadline_epoch) return 0;
    int rc = grade_attempt(db, a, a->deadline_epoch);
    if (rc == EXAM_ERR_ALREADY_SUBMITTED) return 0;
    return rc == EXAM_OK ? 1 : rc;
}

int exam_finalize_expired(sqlite3 *db)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id FROM exam_attempts "
            "WHERE mode = 'EXAM' AND submitted_at IS NULL AND room_id IS NOT NULL",
            -1, &st, NULL) != SQLITE_OK) {
        return set_err(EXAM_ERR_DATABASE);
    }

    /* Doc het id truoc roi moi cap nhat */
    int cap = 16, n = 0;
    int *ids = malloc(sizeof(int) * cap);
    if (ids == NULL) { sqlite3_finalize(st); return set_err(EXAM_ERR_DATABASE); }
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        if (n == cap) {
            cap *= 2;
            int *tmp = realloc(ids, sizeof(int) * cap);
            if (tmp == NULL) { free(ids); sqlite3_finalize(st); return set_err(EXAM_ERR_DATABASE); }
            ids = tmp;
        }
        ids[n++] = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { free(ids); return set_err(EXAM_ERR_DATABASE); }

    int graded = 0;
    for (int i = 0; i < n; i++) {
        AttemptInfo a;
        if (load_attempt(db, ids[i], 0, &a) != EXAM_OK) continue;
        int r = finalize_if_expired(db, &a);
        if (r < 0) { free(ids); return set_err(r); }
        graded += r;
    }
    free(ids);
    set_err(EXAM_OK);
    return graded;
}

/* ---------- EXAM_START ---------- */

int exam_start(sqlite3 *db, int room_id, int user_id)
{
    if (db == NULL || room_id <= 0 || user_id <= 0) return set_err(EXAM_ERR_INVALID_PAYLOAD);

    /* Cham cac bai het gio truoc de khong bi chan boi luot thi cu */
    if (exam_finalize_expired(db) < 0) return set_err(EXAM_ERR_DATABASE);

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT status, question_count, "
            "       CAST(strftime('%s', start_time) AS INTEGER), "
            "       CAST(strftime('%s', end_time) AS INTEGER) "
            "FROM rooms WHERE id = ?1",
            -1, &st, NULL) != SQLITE_OK) {
        return set_err(EXAM_ERR_DATABASE);
    }
    sqlite3_bind_int(st, 1, room_id);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return set_err(rc == SQLITE_DONE ? EXAM_ERR_ROOM_NOT_FOUND : EXAM_ERR_DATABASE);
    }
    char status[16] = {0};
    const unsigned char *s = sqlite3_column_text(st, 0);
    if (s != NULL) snprintf(status, sizeof(status), "%s", (const char *)s);
    int has_start = sqlite3_column_type(st, 2) != SQLITE_NULL;
    long long start_epoch = sqlite3_column_int64(st, 2);
    int has_end = sqlite3_column_type(st, 3) != SQLITE_NULL;
    long long end_epoch = sqlite3_column_int64(st, 3);
    sqlite3_finalize(st);

    int member = exists_2int(db,
        "SELECT 1 FROM room_members WHERE room_id = ?1 AND user_id = ?2", room_id, user_id);
    if (member < 0) return set_err(EXAM_ERR_DATABASE);
    if (member == 0) return set_err(EXAM_ERR_NOT_ROOM_MEMBER);

    /* Chi thi khi phong dang dien ra va nam trong khung gio (neu co) */
    long long now = now_epoch();
    if (strcmp(status, "IN_PROGRESS") != 0 ||
        (has_start && now < start_epoch) ||
        (has_end && now >= end_epoch)) {
        return set_err(EXAM_ERR_EXAM_NOT_AVAILABLE);
    }

    int running = exists_2int(db,
        "SELECT 1 FROM exam_attempts "
        "WHERE room_id = ?1 AND user_id = ?2 AND mode = 'EXAM' AND submitted_at IS NULL",
        room_id, user_id);
    if (running < 0) return set_err(EXAM_ERR_DATABASE);
    if (running > 0) return set_err(EXAM_ERR_ATTEMPT_ALREADY_STARTED);

    /* Bo de cua phong do phan Room (Tuan Anh) thiet lap trong room_questions */
    int total = count_total(db, room_id);
    if (total < 0) return set_err(EXAM_ERR_DATABASE);
    if (total == 0) return set_err(EXAM_ERR_EXAM_NOT_AVAILABLE);

    char started_at[32];
    format_iso(now, started_at, sizeof(started_at));
    if (sqlite3_prepare_v2(db,
            "INSERT INTO exam_attempts (room_id, user_id, mode, started_at) "
            "VALUES (?1, ?2, 'EXAM', ?3)",
            -1, &st, NULL) != SQLITE_OK) {
        return set_err(EXAM_ERR_DATABASE);
    }
    sqlite3_bind_int(st, 1, room_id);
    sqlite3_bind_int(st, 2, user_id);
    sqlite3_bind_text(st, 3, started_at, -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return set_err(EXAM_ERR_DATABASE);

    set_err(EXAM_OK);
    return (int)sqlite3_last_insert_rowid(db);
}

/* Chuyen cJSON thanh chuoi va giai phong cay JSON */
static char *json_finish(cJSON *root)
{
    if (root == NULL) { set_err(EXAM_ERR_DATABASE); return NULL; }
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (out == NULL) { set_err(EXAM_ERR_DATABASE); return NULL; }
    set_err(EXAM_OK);
    return out;
}

static void add_text(cJSON *obj, const char *key, const unsigned char *val)
{
    cJSON_AddStringToObject(obj, key, val != NULL ? (const char *)val : "");
}

char *exam_start_data(sqlite3 *db, int attempt_id, int user_id)
{
    if (db == NULL || attempt_id <= 0 || user_id <= 0) { set_err(EXAM_ERR_INVALID_PAYLOAD); return NULL; }

    AttemptInfo a;
    int rc = load_attempt(db, attempt_id, user_id, &a);
    if (rc != EXAM_OK) { set_err(rc); return NULL; }
    if (a.submitted) { set_err(EXAM_ERR_ALREADY_SUBMITTED); return NULL; }

    char started_at[32], deadline[32];
    format_iso(a.started_epoch, started_at, sizeof(started_at));
    format_iso(a.deadline_epoch, deadline, sizeof(deadline));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "attempt_id", a.attempt_id);
    cJSON_AddNumberToObject(root, "room_id", a.room_id);
    cJSON_AddNumberToObject(root, "duration_minutes", a.duration_minutes);
    cJSON_AddStringToObject(root, "started_at", started_at);
    cJSON_AddStringToObject(root, "deadline", deadline);
    cJSON *questions = cJSON_AddArrayToObject(root, "questions");

    sqlite3_stmt *qs = NULL, *as = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT q.id, q.content, q.topic, q.difficulty "
            "FROM room_questions rq JOIN questions q ON q.id = rq.question_id "
            "WHERE rq.room_id = ?1 ORDER BY rq.position",
            -1, &qs, NULL) != SQLITE_OK ||
        /* Khong SELECT is_correct de khong lo dap an dung */
        sqlite3_prepare_v2(db,
            "SELECT id, content FROM answers WHERE question_id = ?1 ORDER BY id",
            -1, &as, NULL) != SQLITE_OK) {
        sqlite3_finalize(qs);
        cJSON_Delete(root);
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }

    sqlite3_bind_int(qs, 1, a.room_id);
    while ((rc = sqlite3_step(qs)) == SQLITE_ROW) {
        cJSON *q = cJSON_CreateObject();
        int qid = sqlite3_column_int(qs, 0);
        cJSON_AddNumberToObject(q, "question_id", qid);
        add_text(q, "content", sqlite3_column_text(qs, 1));
        add_text(q, "topic", sqlite3_column_text(qs, 2));
        add_text(q, "difficulty", sqlite3_column_text(qs, 3));
        cJSON *answers = cJSON_AddArrayToObject(q, "answers");

        sqlite3_reset(as);
        sqlite3_bind_int(as, 1, qid);
        while (sqlite3_step(as) == SQLITE_ROW) {
            cJSON *ans = cJSON_CreateObject();
            cJSON_AddNumberToObject(ans, "answer_id", sqlite3_column_int(as, 0));
            add_text(ans, "content", sqlite3_column_text(as, 1));
            cJSON_AddItemToArray(answers, ans);
        }
        cJSON_AddItemToArray(questions, q);
    }
    sqlite3_finalize(qs);
    sqlite3_finalize(as);
    if (rc != SQLITE_DONE) { cJSON_Delete(root); set_err(EXAM_ERR_DATABASE); return NULL; }

    return json_finish(root);
}

/* ---------- EXAM_ANSWER ---------- */

int exam_answer(sqlite3 *db, int attempt_id, int user_id, int question_id, int answer_id)
{
    if (db == NULL || attempt_id <= 0 || user_id <= 0 || question_id <= 0 || answer_id <= 0) {
        return set_err(EXAM_ERR_INVALID_PAYLOAD);
    }

    AttemptInfo a;
    int rc = load_attempt(db, attempt_id, user_id, &a);
    if (rc != EXAM_OK) return set_err(rc);
    if (a.submitted) return set_err(EXAM_ERR_ALREADY_SUBMITTED);

    rc = finalize_if_expired(db, &a);
    if (rc < 0) return set_err(rc);
    if (rc > 0) return set_err(EXAM_ERR_TIME_EXPIRED);

    rc = exists_2int(db,
        "SELECT 1 FROM room_questions WHERE room_id = ?1 AND question_id = ?2",
        a.room_id, question_id);
    if (rc < 0) return set_err(EXAM_ERR_DATABASE);
    if (rc == 0) return set_err(EXAM_ERR_QUESTION_NOT_IN_EXAM);

    rc = exists_2int(db,
        "SELECT 1 FROM answers WHERE id = ?1 AND question_id = ?2",
        answer_id, question_id);
    if (rc < 0) return set_err(EXAM_ERR_DATABASE);
    if (rc == 0) return set_err(EXAM_ERR_ANSWER_NOT_IN_QUESTION);

    /* Doi dap an thi cap nhat ban ghi cu, khong tao ban ghi thu hai */
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "INSERT INTO attempt_answers (attempt_id, question_id, answer_id) "
            "VALUES (?1, ?2, ?3) "
            "ON CONFLICT (attempt_id, question_id) DO UPDATE SET answer_id = excluded.answer_id",
            -1, &st, NULL) != SQLITE_OK) {
        return set_err(EXAM_ERR_DATABASE);
    }
    sqlite3_bind_int(st, 1, attempt_id);
    sqlite3_bind_int(st, 2, question_id);
    sqlite3_bind_int(st, 3, answer_id);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) return set_err(EXAM_ERR_DATABASE);

    return set_err(EXAM_OK);
}

/* ---------- EXAM_SUBMIT ---------- */

int exam_submit(sqlite3 *db, int attempt_id, int user_id)
{
    if (db == NULL || attempt_id <= 0 || user_id <= 0) return set_err(EXAM_ERR_INVALID_PAYLOAD);

    AttemptInfo a;
    int rc = load_attempt(db, attempt_id, user_id, &a);
    if (rc != EXAM_OK) return set_err(rc);
    if (a.submitted) return set_err(EXAM_ERR_ALREADY_SUBMITTED);

    /* Nop sau han: cham theo cac dap an da luu, thoi diem nop tinh bang han nop */
    rc = finalize_if_expired(db, &a);
    if (rc < 0) return set_err(rc);
    if (rc > 0) return set_err(EXAM_OK);

    return set_err(grade_attempt(db, &a, now_epoch()));
}

/* ---------- EXAM_RESULT ---------- */

char *exam_result(sqlite3 *db, int attempt_id, int user_id)
{
    if (db == NULL || attempt_id <= 0 || user_id <= 0) { set_err(EXAM_ERR_INVALID_PAYLOAD); return NULL; }

    AttemptInfo a;
    int rc = load_attempt(db, attempt_id, user_id, &a);
    if (rc != EXAM_OK) { set_err(rc); return NULL; }

    rc = finalize_if_expired(db, &a);
    if (rc < 0) { set_err(rc); return NULL; }
    if (!a.submitted) { set_err(EXAM_ERR_NOT_SUBMITTED); return NULL; }

    int total = count_total(db, a.room_id);
    int correct = count_correct(db, a.attempt_id, a.room_id);
    if (total < 0 || correct < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }

    char started_at[32], submitted_at[32];
    format_iso(a.started_epoch, started_at, sizeof(started_at));
    format_iso(a.submitted_epoch, submitted_at, sizeof(submitted_at));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "attempt_id", a.attempt_id);
    cJSON_AddNumberToObject(root, "room_id", a.room_id);
    cJSON_AddNumberToObject(root, "total_questions", total);
    cJSON_AddNumberToObject(root, "correct_count", correct);
    cJSON_AddNumberToObject(root, "score", a.score);
    cJSON_AddStringToObject(root, "started_at", started_at);
    cJSON_AddStringToObject(root, "submitted_at", submitted_at);
    return json_finish(root);
}

/* ---------- EXAM_HISTORY ---------- */

char *exam_history(sqlite3 *db, int user_id)
{
    if (db == NULL || user_id <= 0) { set_err(EXAM_ERR_INVALID_PAYLOAD); return NULL; }
    if (exam_finalize_expired(db) < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT a.id, a.room_id, r.name, a.score, "
            "       CAST(strftime('%s', a.started_at) AS INTEGER), "
            "       CAST(strftime('%s', a.submitted_at) AS INTEGER) "
            "FROM exam_attempts a LEFT JOIN rooms r ON r.id = a.room_id "
            "WHERE a.user_id = ?1 AND a.mode = 'EXAM' AND a.submitted_at IS NOT NULL "
            "ORDER BY CAST(strftime('%s', a.started_at) AS INTEGER) DESC, a.id DESC",
            -1, &st, NULL) != SQLITE_OK) {
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }
    sqlite3_bind_int(st, 1, user_id);

    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "attempts");
    int rc;
    while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
        char started_at[32], submitted_at[32];
        format_iso(sqlite3_column_int64(st, 4), started_at, sizeof(started_at));
        format_iso(sqlite3_column_int64(st, 5), submitted_at, sizeof(submitted_at));

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "attempt_id", sqlite3_column_int(st, 0));
        cJSON_AddNumberToObject(item, "room_id", sqlite3_column_int(st, 1));
        add_text(item, "room_name", sqlite3_column_text(st, 2));
        cJSON_AddNumberToObject(item, "score", sqlite3_column_double(st, 3));
        cJSON_AddStringToObject(item, "started_at", started_at);
        cJSON_AddStringToObject(item, "submitted_at", submitted_at);
        cJSON_AddItemToArray(list, item);
    }
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) { cJSON_Delete(root); set_err(EXAM_ERR_DATABASE); return NULL; }

    return json_finish(root);
}

/* ---------- Ma loi ---------- */

const char *exam_error_code(int err)
{
    switch (err) {
    case EXAM_OK:                          return "OK";
    case EXAM_ERR_INVALID_PAYLOAD:         return "INVALID_PAYLOAD";
    case EXAM_ERR_ROOM_NOT_FOUND:          return "ROOM_NOT_FOUND";
    case EXAM_ERR_NOT_ROOM_MEMBER:         return "NOT_ROOM_MEMBER";
    case EXAM_ERR_EXAM_NOT_AVAILABLE:      return "EXAM_NOT_AVAILABLE";
    case EXAM_ERR_ATTEMPT_ALREADY_STARTED: return "ATTEMPT_ALREADY_STARTED";
    case EXAM_ERR_ATTEMPT_NOT_FOUND:       return "ATTEMPT_NOT_FOUND";
    case EXAM_ERR_ALREADY_SUBMITTED:       return "EXAM_ALREADY_SUBMITTED";
    case EXAM_ERR_TIME_EXPIRED:            return "EXAM_TIME_EXPIRED";
    case EXAM_ERR_QUESTION_NOT_IN_EXAM:    return "QUESTION_NOT_IN_EXAM";
    case EXAM_ERR_ANSWER_NOT_IN_QUESTION:  return "ANSWER_NOT_IN_QUESTION";
    case EXAM_ERR_NOT_SUBMITTED:           return "EXAM_NOT_SUBMITTED";
    case EXAM_ERR_DATABASE:                return "DATABASE_ERROR";
    case EXAM_ERR_FORBIDDEN:               return "FORBIDDEN";
    case EXAM_ERR_ROOM_NOT_FINISHED:       return "ROOM_NOT_FINISHED";
    default:                               return "INTERNAL_ERROR";
    }
}

const char *exam_error_message(int err)
{
    switch (err) {
    case EXAM_OK:                          return "Thành công";
    case EXAM_ERR_INVALID_PAYLOAD:         return "Dữ liệu không hợp lệ";
    case EXAM_ERR_ROOM_NOT_FOUND:          return "Phòng không tồn tại";
    case EXAM_ERR_NOT_ROOM_MEMBER:         return "Bạn chưa tham gia phòng này";
    case EXAM_ERR_EXAM_NOT_AVAILABLE:      return "Phòng chưa cho phép bắt đầu thi";
    case EXAM_ERR_ATTEMPT_ALREADY_STARTED: return "Bạn đang có lượt thi chưa nộp trong phòng này";
    case EXAM_ERR_ATTEMPT_NOT_FOUND:       return "Lượt thi không tồn tại";
    case EXAM_ERR_ALREADY_SUBMITTED:       return "Bài thi đã được nộp";
    case EXAM_ERR_TIME_EXPIRED:            return "Đã hết thời gian làm bài";
    case EXAM_ERR_QUESTION_NOT_IN_EXAM:    return "Câu hỏi không thuộc bài thi";
    case EXAM_ERR_ANSWER_NOT_IN_QUESTION:  return "Đáp án không thuộc câu hỏi";
    case EXAM_ERR_NOT_SUBMITTED:           return "Bài thi chưa hoàn thành";
    case EXAM_ERR_DATABASE:                return "Lỗi cơ sở dữ liệu";
    case EXAM_ERR_FORBIDDEN:               return "Bạn không có quyền xem nội dung này";
    case EXAM_ERR_ROOM_NOT_FINISHED:       return "Chỉ xem được khi phòng thi đã kết thúc";
    default:                               return "Lỗi xử lý phía server";
    }
}

/* =====================================================================
 * Bo sung: xem lai bai thi (EXAM_REVIEW) va ket qua phong (ROOM_RESULT)
 * ===================================================================== */

typedef struct {
    int room_id;
    int owner_id;
    char name[256];
    char status[16];
    int question_count;
    int duration_minutes;
    int ended;      /* 1 neu phong FINISHED hoac da qua end_time */
} RoomInfo;

static int load_room(sqlite3 *db, int room_id, RoomInfo *out)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT id, owner_id, name, status, question_count, duration_minutes, "
            "       CAST(strftime('%s', end_time) AS INTEGER) "
            "FROM rooms WHERE id = ?1",
            -1, &st, NULL) != SQLITE_OK) {
        return EXAM_ERR_DATABASE;
    }
    sqlite3_bind_int(st, 1, room_id);
    int rc = sqlite3_step(st);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(st);
        return rc == SQLITE_DONE ? EXAM_ERR_ROOM_NOT_FOUND : EXAM_ERR_DATABASE;
    }
    memset(out, 0, sizeof(*out));
    out->room_id = sqlite3_column_int(st, 0);
    out->owner_id = sqlite3_column_int(st, 1);
    const unsigned char *t = sqlite3_column_text(st, 2);
    snprintf(out->name, sizeof(out->name), "%s", t != NULL ? (const char *)t : "");
    t = sqlite3_column_text(st, 3);
    snprintf(out->status, sizeof(out->status), "%s", t != NULL ? (const char *)t : "");
    out->question_count = sqlite3_column_int(st, 4);
    out->duration_minutes = sqlite3_column_int(st, 5);
    out->ended = strcmp(out->status, "FINISHED") == 0 ||
                 (sqlite3_column_type(st, 6) != SQLITE_NULL &&
                  now_epoch() >= sqlite3_column_int64(st, 6));
    sqlite3_finalize(st);
    return EXAM_OK;
}

/* 1 neu nguoi dung la ADMIN, 0 neu khong, am neu loi */
static int is_admin(sqlite3 *db, int user_id)
{
    return exists_2int(db, "SELECT 1 FROM users WHERE id = ?1 AND role = 'ADMIN' AND ?2 = ?2", user_id, 0);
}

/* 1 neu duoc quan ly phong (chu phong hoac ADMIN) */
static int can_manage_room(sqlite3 *db, const RoomInfo *room, int user_id)
{
    if (room->owner_id == user_id) return 1;
    return is_admin(db, user_id);
}

char *exam_review(sqlite3 *db, int attempt_id, int user_id)
{
    if (db == NULL || attempt_id <= 0 || user_id <= 0) { set_err(EXAM_ERR_INVALID_PAYLOAD); return NULL; }

    AttemptInfo a;
    int rc = load_attempt(db, attempt_id, 0, &a);
    if (rc != EXAM_OK) { set_err(rc); return NULL; }

    RoomInfo room;
    rc = load_room(db, a.room_id, &room);
    if (rc != EXAM_OK) { set_err(rc); return NULL; }

    int manager = can_manage_room(db, &room, user_id);
    if (manager < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }
    /* Khong phai bai cua minh va khong quan ly phong: coi nhu khong ton tai */
    if (!manager && a.user_id != user_id) { set_err(EXAM_ERR_ATTEMPT_NOT_FOUND); return NULL; }

    rc = finalize_if_expired(db, &a);
    if (rc < 0) { set_err(rc); return NULL; }
    if (!a.submitted) { set_err(EXAM_ERR_NOT_SUBMITTED); return NULL; }
    /* Thi sinh chi xem dap an khi phong da ket thuc, tranh lo de cho nguoi dang thi */
    if (!manager && !room.ended) { set_err(EXAM_ERR_ROOM_NOT_FINISHED); return NULL; }

    int total = count_total(db, a.room_id);
    int correct = count_correct(db, a.attempt_id, a.room_id);
    if (total < 0 || correct < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }

    char started_at[32], submitted_at[32];
    format_iso(a.started_epoch, started_at, sizeof(started_at));
    format_iso(a.submitted_epoch, submitted_at, sizeof(submitted_at));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "attempt_id", a.attempt_id);
    cJSON_AddNumberToObject(root, "room_id", a.room_id);
    cJSON_AddStringToObject(root, "room_name", room.name);
    cJSON_AddNumberToObject(root, "user_id", a.user_id);
    cJSON_AddNumberToObject(root, "total_questions", total);
    cJSON_AddNumberToObject(root, "correct_count", correct);
    cJSON_AddNumberToObject(root, "score", a.score);
    cJSON_AddStringToObject(root, "started_at", started_at);
    cJSON_AddStringToObject(root, "submitted_at", submitted_at);

    sqlite3_stmt *un = NULL;
    if (sqlite3_prepare_v2(db, "SELECT username FROM users WHERE id = ?1", -1, &un, NULL) == SQLITE_OK) {
        sqlite3_bind_int(un, 1, a.user_id);
        if (sqlite3_step(un) == SQLITE_ROW) add_text(root, "username", sqlite3_column_text(un, 0));
    }
    sqlite3_finalize(un);

    cJSON *questions = cJSON_AddArrayToObject(root, "questions");
    sqlite3_stmt *qs = NULL, *as = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT rq.position, q.id, q.content, q.topic, q.difficulty, aa.answer_id "
            "FROM room_questions rq JOIN questions q ON q.id = rq.question_id "
            "LEFT JOIN attempt_answers aa ON aa.attempt_id = ?2 AND aa.question_id = q.id "
            "WHERE rq.room_id = ?1 ORDER BY rq.position",
            -1, &qs, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(db,
            "SELECT id, content, is_correct FROM answers WHERE question_id = ?1 ORDER BY id",
            -1, &as, NULL) != SQLITE_OK) {
        sqlite3_finalize(qs);
        cJSON_Delete(root);
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }
    sqlite3_bind_int(qs, 1, a.room_id);
    sqlite3_bind_int(qs, 2, a.attempt_id);

    while ((rc = sqlite3_step(qs)) == SQLITE_ROW) {
        int qid = sqlite3_column_int(qs, 1);
        int has_selected = sqlite3_column_type(qs, 5) != SQLITE_NULL;
        int selected = sqlite3_column_int(qs, 5);

        cJSON *q = cJSON_CreateObject();
        cJSON_AddNumberToObject(q, "position", sqlite3_column_int(qs, 0));
        cJSON_AddNumberToObject(q, "question_id", qid);
        add_text(q, "content", sqlite3_column_text(qs, 2));
        add_text(q, "topic", sqlite3_column_text(qs, 3));
        add_text(q, "difficulty", sqlite3_column_text(qs, 4));
        if (has_selected) cJSON_AddNumberToObject(q, "selected_answer_id", selected);
        else cJSON_AddNullToObject(q, "selected_answer_id");

        int correct_id = 0, selected_ok = 0;
        cJSON *answers = cJSON_CreateArray();
        sqlite3_reset(as);
        sqlite3_bind_int(as, 1, qid);
        while (sqlite3_step(as) == SQLITE_ROW) {
            int aid = sqlite3_column_int(as, 0);
            int ok = sqlite3_column_int(as, 2);
            if (ok && correct_id == 0) correct_id = aid;
            if (ok && has_selected && aid == selected) selected_ok = 1;
            cJSON *ans = cJSON_CreateObject();
            cJSON_AddNumberToObject(ans, "answer_id", aid);
            add_text(ans, "content", sqlite3_column_text(as, 1));
            cJSON_AddBoolToObject(ans, "is_correct", ok);
            cJSON_AddItemToArray(answers, ans);
        }
        cJSON_AddNumberToObject(q, "correct_answer_id", correct_id);
        cJSON_AddStringToObject(q, "result",
            !has_selected ? "UNANSWERED" : (selected_ok ? "CORRECT" : "WRONG"));
        cJSON_AddItemToObject(q, "answers", answers);
        cJSON_AddItemToArray(questions, q);
    }
    sqlite3_finalize(qs);
    sqlite3_finalize(as);
    if (rc != SQLITE_DONE) { cJSON_Delete(root); set_err(EXAM_ERR_DATABASE); return NULL; }

    return json_finish(root);
}

char *exam_room_result(sqlite3 *db, int room_id, int user_id)
{
    if (db == NULL || room_id <= 0 || user_id <= 0) { set_err(EXAM_ERR_INVALID_PAYLOAD); return NULL; }
    if (exam_finalize_expired(db) < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }

    RoomInfo room;
    int rc = load_room(db, room_id, &room);
    if (rc != EXAM_OK) { set_err(rc); return NULL; }

    /* Chi giao vien (chu phong hoac ADMIN) xem ket qua cua ca phong */
    int manager = can_manage_room(db, &room, user_id);
    if (manager < 0) { set_err(EXAM_ERR_DATABASE); return NULL; }
    if (!manager) { set_err(EXAM_ERR_FORBIDDEN); return NULL; }

    /* Thong ke so luong (khong tinh chu phong la thi sinh) */
    int member_count = count_2int(db,
        "SELECT COUNT(*) FROM room_members WHERE room_id = ?1 AND user_id <> ?2",
        room_id, room.owner_id);
    int submitted_count = count_2int(db,
        "SELECT COUNT(DISTINCT user_id) FROM exam_attempts "
        "WHERE room_id = ?1 AND mode = 'EXAM' AND submitted_at IS NOT NULL AND ?2 = ?2",
        room_id, 0);
    int in_progress_count = count_2int(db,
        "SELECT COUNT(DISTINCT user_id) FROM exam_attempts "
        "WHERE room_id = ?1 AND mode = 'EXAM' AND submitted_at IS NULL AND ?2 = ?2",
        room_id, 0);
    int total = count_total(db, room_id);
    if (member_count < 0 || submitted_count < 0 || in_progress_count < 0 || total < 0) {
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "room_id", room.room_id);
    cJSON_AddStringToObject(root, "room_name", room.name);
    cJSON_AddNumberToObject(root, "owner_id", room.owner_id);
    cJSON_AddStringToObject(root, "status", room.status);
    cJSON_AddNumberToObject(root, "question_count", room.question_count);
    cJSON_AddNumberToObject(root, "duration_minutes", room.duration_minutes);
    cJSON_AddNumberToObject(root, "member_count", member_count);
    cJSON_AddNumberToObject(root, "submitted_count", submitted_count);
    cJSON_AddNumberToObject(root, "in_progress_count", in_progress_count);

    /* Ket qua tung thi sinh: moi nguoi lay luot co diem cao nhat (luot doc truoc nhat neu bang diem).
       Sap xep theo ten; bang xep hang thuoc phan cua Tuan Anh nen khong xep hang o day */
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db,
            "SELECT a.id, a.user_id, u.username, a.score, "
            "       CAST(strftime('%s', a.started_at) AS INTEGER) AS s, "
            "       CAST(strftime('%s', a.submitted_at) AS INTEGER) AS e "
            "FROM exam_attempts a JOIN users u ON u.id = a.user_id "
            "WHERE a.room_id = ?1 AND a.mode = 'EXAM' AND a.submitted_at IS NOT NULL "
            "ORDER BY u.username COLLATE NOCASE, a.user_id, a.score DESC, a.id ASC",
            -1, &st, NULL) != SQLITE_OK) {
        cJSON_Delete(root);
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }
    sqlite3_bind_int(st, 1, room_id);

    cJSON *results = cJSON_CreateArray();
    int seen_cap = 16, seen_n = 0;
    int *seen = malloc(sizeof(int) * seen_cap);
    double sum = 0, best = 0, worst = 0;

    while (seen != NULL && (rc = sqlite3_step(st)) == SQLITE_ROW) {
        int uid = sqlite3_column_int(st, 1);
        int dup = 0;
        for (int i = 0; i < seen_n; i++) if (seen[i] == uid) { dup = 1; break; }
        if (dup) continue;
        if (seen_n == seen_cap) {
            seen_cap *= 2;
            int *tmp = realloc(seen, sizeof(int) * seen_cap);
            if (tmp == NULL) { free(seen); seen = NULL; break; }
            seen = tmp;
        }
        seen[seen_n++] = uid;

        int attempt_id = sqlite3_column_int(st, 0);
        double score = sqlite3_column_double(st, 3);
        long long s = sqlite3_column_int64(st, 4), e = sqlite3_column_int64(st, 5);
        long long dur = e - s;

        if (seen_n == 1) { best = score; worst = score; }
        if (score > best) best = score;
        if (score < worst) worst = score;
        sum += score;

        char started_at[32], submitted_at[32];
        format_iso(s, started_at, sizeof(started_at));
        format_iso(e, submitted_at, sizeof(submitted_at));

        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "user_id", uid);
        add_text(item, "username", sqlite3_column_text(st, 2));
        cJSON_AddNumberToObject(item, "attempt_id", attempt_id);
        cJSON_AddNumberToObject(item, "score", score);
        cJSON_AddNumberToObject(item, "correct_count", count_correct(db, attempt_id, room_id));
        cJSON_AddNumberToObject(item, "total_questions", total);
        cJSON_AddStringToObject(item, "started_at", started_at);
        cJSON_AddStringToObject(item, "submitted_at", submitted_at);
        cJSON_AddNumberToObject(item, "duration_seconds", (double)dur);
        cJSON_AddItemToArray(results, item);
    }
    sqlite3_finalize(st);
    free(seen);
    if (seen == NULL || (rc != SQLITE_DONE && rc != SQLITE_ROW)) {
        cJSON_Delete(results);
        cJSON_Delete(root);
        set_err(EXAM_ERR_DATABASE);
        return NULL;
    }

    if (seen_n > 0) {
        cJSON_AddNumberToObject(root, "average_score", round2(sum / seen_n));
        cJSON_AddNumberToObject(root, "highest_score", best);
        cJSON_AddNumberToObject(root, "lowest_score", worst);
    } else {
        cJSON_AddNullToObject(root, "average_score");
        cJSON_AddNullToObject(root, "highest_score");
        cJSON_AddNullToObject(root, "lowest_score");
    }
    cJSON_AddItemToObject(root, "results", results);

    /* Danh sach thi sinh chua thi */
    cJSON *not_started = cJSON_AddArrayToObject(root, "not_started");
    if (sqlite3_prepare_v2(db,
            "SELECT u.id, u.username FROM room_members m JOIN users u ON u.id = m.user_id "
            "WHERE m.room_id = ?1 AND m.user_id <> ?2 AND NOT EXISTS ("
            "  SELECT 1 FROM exam_attempts a WHERE a.room_id = m.room_id "
            "  AND a.user_id = m.user_id AND a.mode = 'EXAM') "
            "ORDER BY u.username",
            -1, &st, NULL) == SQLITE_OK) {
        sqlite3_bind_int(st, 1, room_id);
        sqlite3_bind_int(st, 2, room.owner_id);
        while (sqlite3_step(st) == SQLITE_ROW) {
            cJSON *u = cJSON_CreateObject();
            cJSON_AddNumberToObject(u, "user_id", sqlite3_column_int(st, 0));
            add_text(u, "username", sqlite3_column_text(st, 1));
            cJSON_AddItemToArray(not_started, u);
        }
        sqlite3_finalize(st);
    }
    int not_started_count = count_2int(db,
        "SELECT COUNT(*) FROM room_members m WHERE m.room_id = ?1 AND m.user_id <> ?2 "
        "AND NOT EXISTS (SELECT 1 FROM exam_attempts a WHERE a.room_id = m.room_id "
        "AND a.user_id = m.user_id AND a.mode = 'EXAM')",
        room_id, room.owner_id);
    cJSON_AddNumberToObject(root, "not_started_count", not_started_count < 0 ? 0 : not_started_count);

    return json_finish(root);
}
