#ifndef EXAM_H
#define EXAM_H

#include <sqlite3.h>

/*
 * Module Exam - Cong Minh
 *
 * Quy uoc chung:
 * - Ham tra ve int: gia tri >= 0 la thanh cong, gia tri am la ma loi (ExamError).
 * - Ham tra ve char *: NULL la that bai, goi exam_last_error() de lay ma loi.
 *   Chuoi JSON tra ve duoc cap phat dong, ben goi phai free().
 * - user_id luon lay tu phien dang nhap, khong lay tu payload cua client.
 * - Chuoi JSON tra ve chinh la truong "data" cua response trong docs/protocol.md.
 */

typedef enum {
    EXAM_OK                          = 0,
    EXAM_ERR_INVALID_PAYLOAD         = -1,
    EXAM_ERR_ROOM_NOT_FOUND          = -2,
    EXAM_ERR_NOT_ROOM_MEMBER         = -3,
    EXAM_ERR_EXAM_NOT_AVAILABLE      = -4,
    EXAM_ERR_ATTEMPT_ALREADY_STARTED = -5,
    EXAM_ERR_ATTEMPT_NOT_FOUND       = -6,
    EXAM_ERR_ALREADY_SUBMITTED       = -7,
    EXAM_ERR_TIME_EXPIRED            = -8,
    EXAM_ERR_QUESTION_NOT_IN_EXAM    = -9,
    EXAM_ERR_ANSWER_NOT_IN_QUESTION  = -10,
    EXAM_ERR_NOT_SUBMITTED           = -11,
    EXAM_ERR_DATABASE                = -12,
    EXAM_ERR_FORBIDDEN               = -13,
    EXAM_ERR_ROOM_NOT_FINISHED       = -14
} ExamError;

/* ===== 5 ham nghiep vu da thong nhat ===== */

/* Bat dau luot thi trong phong, tra ve attempt_id (> 0) hoac ma loi (< 0).
   Phong phai da co bo de trong room_questions (phan Room thiet lap). */
int exam_start(sqlite3 *db, int room_id, int user_id);

/* Luu hoac cap nhat dap an cua mot cau hoi. Tra ve EXAM_OK hoac ma loi */
int exam_answer(sqlite3 *db, int attempt_id, int user_id,
                int question_id, int answer_id);

/* Nop bai va cham diem theo cac dap an da luu. Tra ve EXAM_OK hoac ma loi */
int exam_submit(sqlite3 *db, int attempt_id, int user_id);

/* Lay ket qua cua mot luot thi da hoan thanh */
/* Chuoi JSON tra ve duoc cap phat dong, ben goi phai free() */
char *exam_result(sqlite3 *db, int attempt_id, int user_id);

/* Lay lich su thi cua nguoi dung */
/* Chuoi JSON tra ve duoc cap phat dong, ben goi phai free() */
char *exam_history(sqlite3 *db, int user_id);

/* ===== Ham bo sung: xem lai bai va ket qua phong (de xuat, xem docs/DE_XUAT_API_KET_QUA.md) ===== */

/*
 * EXAM_REVIEW: xem lai bai da nop - dap an da chon, dap an dung, cau dung/sai/bo trong.
 * - Chu phong hoac ADMIN: xem bai cua moi thi sinh trong phong, bat ky luc nao.
 * - Thi sinh: chi xem bai cua minh va chi khi phong da ket thuc (chong lo dap an).
 * Ben goi phai free().
 */
char *exam_review(sqlite3 *db, int attempt_id, int user_id);

/*
 * ROOM_RESULT: ket qua thi cua phong, chi giao vien (chu phong hoac ADMIN) duoc xem.
 * Gom thong ke (si so, da nop, dang lam, chua thi, diem TB/cao nhat/thap nhat),
 * ket qua tung thi sinh (luot diem cao nhat, sap xep theo ten) va danh sach chua thi.
 * Khong xep hang (bang xep hang thuoc phan cua Tuan Anh).
 * Ben goi phai free().
 */
char *exam_room_result(sqlite3 *db, int room_id, int user_id);

/* ===== Ham ho tro tich hop (bo sung, khong doi 5 ham tren) ===== */

/*
 * Tao truong "data" cho EXAM_START_RESULT: attempt_id, deadline, danh sach
 * cau hoi va dap an (KHONG co is_correct). Goi ngay sau exam_start() thanh cong.
 * Ben goi phai free().
 */
char *exam_start_data(sqlite3 *db, int attempt_id, int user_id);

/*
 * Tu dong nop va cham cac luot thi da het gio.
 * Network nen goi dinh ky (vi du moi vong poll()) de khong phu thuoc client.
 * Tra ve so luot thi vua duoc cham, hoac EXAM_ERR_DATABASE.
 */
int exam_finalize_expired(sqlite3 *db);

/* Ma loi cua lan goi gan nhat (dung khi ham tra ve NULL) */
int exam_last_error(void);

/* Doi ma loi sang error.code trong protocol, vi du "ATTEMPT_NOT_FOUND" */
const char *exam_error_code(int err);

/* Doi ma loi sang error.message tieng Viet de hien thi cho nguoi dung */
const char *exam_error_message(int err);

/*
 * Goi y cho Network khi xu ly EXAM_SUBMIT:
 *   int rc = exam_submit(db, attempt_id, user_id);
 *   if (rc == EXAM_OK) data = exam_result(db, attempt_id, user_id);
 * Du lieu cua exam_result() chua du cac truong cua EXAM_SUBMIT_RESULT.
 */

#endif
