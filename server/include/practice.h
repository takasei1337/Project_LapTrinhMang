#ifndef PRACTICE_H
#define PRACTICE_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Practice
 * ============================================================================
 */
#define PRACTICE_SUCCESS                    0  /* Thao tac thanh cong */
#define PRACTICE_ERR_INVALID_PARAM         -1  /* Tham so dau vao khong hop le hoac con tro NULL */
#define PRACTICE_ERR_DATABASE              -2  /* Loi truy van, prepare hoac ghi du lieu SQLite */
#define PRACTICE_ERR_NOT_FOUND             -3  /* Luot luyen tap (attempt_id) khong ton tai hoac khong thuoc user */
#define PRACTICE_ERR_ALREADY_DONE          -4  /* Luot luyen tap da duoc nop truoc do (chong nop 2 lan) */
#define PRACTICE_ERR_NOT_ENOUGH_QUESTIONS  -5  /* Ngan hang cau hoi khong du so cau theo bo loc yeu cau */
#define PRACTICE_ERR_INSUFFICIENT_QUESTIONS -5 /* Thieu so luong cau hoi theo yeu cau cua selector can bang */
#define PRACTICE_ERR_INVALID_ANSWER        -6  /* Dap an khong hop le, khong thuoc cau hoi hoac cau hoi ngoai bo de */
#define PRACTICE_ERR_MEMORY                -7  /* Loi cap phat bo nho dong */

/*
 * ============================================================================
 * API: practice_start
 * ============================================================================
 *
 * Khoi tao mot luot luyen tap trac nghiem ca nhan moi:
 * - Kiem tra hop le cac tham so dau vao.
 * - Chon ngau nhien N cau hoi tu bang questions theo bo loc topic va difficulty.
 * - Tao ban ghi moi trong exam_attempts voi mode = 'PRACTICE', room_id = NULL.
 * - Ghi co dinh danh sach cau hoi vao bang practice_questions theo vi tri position > 0.
 * - Dong goi danh sach cau hoi va cac lua chon dap an thanh chuoi JSON (tuyet doi
 *   khong chua truong is_correct hoac dap an dung).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - user_id: ID nguoi dung thuc hien luyen tap (> 0).
 * - topic: Chu de can luyen tap (co the NULL neu khong loc theo chu de).
 * - difficulty: Do kho 'EASY'|'MEDIUM'|'HARD' (co the NULL neu khong loc theo do kho).
 * - num_questions: So luong cau hoi muon luyen tap (> 0).
 * - out_questions_json: Con tro nhan chuoi JSON danh sach cau hoi.
 *
 * Hop dong bo nho:
 * - Khi thanh cong, *out_questions_json tro toi chuoi JSON duoc cap phat dong.
 *   Caller (ben goi) so huu chuoi nay va phai goi free() sau khi su dung.
 * - Khi that bai, neu out_questions_json != NULL thi *out_questions_json duoc gan NULL.
 *
 * Tra ve:
 * - attempt_id duong (> 0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi cac ma loi PRACTICE_ERR_* khi that bai.
 */
int practice_start(sqlite3 *db,
                   int user_id,
                   const char *topic,
                   const char *difficulty,
                   int num_questions,
                   char **out_questions_json);

/*
 * ============================================================================
 * API: practice_start_balanced
 * ============================================================================
 *
 * Khoi tao luot luyen tap moi su dung bo chon can bang Balanced Question Selector:
 * - Chon ngau nhien N cau hoi can bang giua cac Topic theo cay phan cap
 *   Subject -> Chapter -> Lesson -> Topic.
 * - Goi question_selector_balanced() / question_selector_balanced_ex() de chon cau hoi.
 * - Neu ngan hang cau hoi khong du so cau yeu cau (hoac 0 cau):
 *   KHONG tao attempt do dang, rollback transaction va tra ve
 *   PRACTICE_ERR_INSUFFICIENT_QUESTIONS (-5).
 * - Ghi nhan exam_attempts (mode = 'PRACTICE', room_id = NULL) va practice_questions.
 * - Dong goi JSON tra ve cho client gom cac cau hoi va lua chon dap an
 *   (tuyet doi khong chua truong is_correct).
 * - Tra ve attempt_id (> 0) khi thanh cong.
 * - Tra ve ma am (< 0) tuong ung voi cac ma loi PRACTICE_ERR_* khi that bai.
 */
int practice_start_balanced(sqlite3 *db,
                            int user_id,
                            int subject_id,
                            int chapter_id,
                            int lesson_id,
                            const char *difficulty,
                            int num_questions,
                            char **out_questions_json);

/*
 * Phien ban mo rong bo sung topic_id:
 */
int practice_start_balanced_ex(sqlite3 *db,
                               int user_id,
                               int subject_id,
                               int chapter_id,
                               int lesson_id,
                               int topic_id,
                               const char *difficulty,
                               int num_questions,
                               char **out_questions_json);

/*
 * ============================================================================
 * API: practice_submit
 * ============================================================================
 *
 * Nop bai luyen tap theo mo hinh Batch Submit:
 * - Nhan toan bo dap an ma thi sinh da chon qua chuoi JSON answers_json.
 *   Dinh dang answers_json:
 *   [
 *     {"question_id": 1, "answer_id": 10},
 *     {"question_id": 2, "answer_id": 14}
 *   ]
 * - Xac minh attempt_id ton tai, thuoc ve dung user_id, co mode = 'PRACTICE',
 *   room_id = NULL va chua tung duoc nop (submitted_at IS NULL).
 * - Xac minh tung question_id phai nam trong bo de practice_questions cua attempt do.
 *   Khong chap nhan question_id xuat hien trung lap trong mang.
 * - Xac minh tung answer_id phai thuoc ve dung question_id tuong ung.
 * - Cho phep thi sinh bo qua cau chua tra loi (nhung cau hoi trong practice_questions
 *   khong xuat hien trong answers_json se duoc tinh la chua tra loi va tinh 0 diem).
 * - Luu toan bo dap an hop le vao attempt_answers (khong nop mot phan neu co loi).
 * - Tinh diem tren thang 10:
 *     score = 10.0 * correct_count / total_questions
 *   Trong do total_questions duoc dem truc tiep tu practice_questions cua attempt,
 *   tuyet doi khong tin gia tri tu phia client.
 * - Cap nhat score va submitted_at = CURRENT_TIMESTAMP vao exam_attempts.
 * - Dong goi ket qua cham diem (score, correct_count, total_questions, chi tiet dap an
 *   dung tung cau) thanh chuoi JSON tra ve qua out_result_json.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - attempt_id: ID luot luyen tap (> 0).
 * - user_id: ID nguoi dung nop bai (> 0).
 * - answers_json: Chuoi JSON mang danh sach cau tra loi (khong duoc NULL).
 * - out_result_json: Con tro nhan chuoi JSON ket qua cham diem chi tiet.
 *
 * Hop dong bo nho:
 * - Khi thanh cong, *out_result_json tro toi chuoi JSON duoc cap phat dong.
 *   Caller (ben goi) so huu chuoi nay va phai goi free() sau khi su dung.
 * - Khi that bai, neu out_result_json != NULL thi *out_result_json duoc gan NULL.
 *
 * Tra ve:
 * - PRACTICE_SUCCESS (0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi cac ma loi PRACTICE_ERR_* khi that bai.
 */
int practice_submit(sqlite3 *db,
                    int attempt_id,
                    int user_id,
                    const char *answers_json,
                    char **out_result_json);

#endif /* PRACTICE_H */

