#ifndef QUESTION_H
#define QUESTION_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Question
 * ============================================================================
 */
#define QUESTION_SUCCESS             0  /* Thao tac thanh cong */
#define QUESTION_ERR_INVALID_PARAM  -1  /* Tham so dau vao khong hop le hoac con tro NULL */
#define QUESTION_ERR_DATABASE       -2  /* Loi truy van, prepare hoac ghi du lieu SQLite */
#define QUESTION_ERR_NOT_FOUND      -3  /* Cau hoi khong ton tai */
#define QUESTION_ERR_INVALID_DATA   -4  /* Du lieu dap an khong dat tieu chuan (< 2 dap an, hoac != 1 dap an dung) */
#define QUESTION_ERR_IN_USE         -5  /* Cau hoi da duoc su dung trong bai thi/luyen tap, khong the sua/xoa */
#define QUESTION_ERR_MEMORY         -6  /* Loi cap phat bo nho dong */

/*
 * ============================================================================
 * API: question_create
 * ============================================================================
 *
 * Them mot cau hoi moi kem danh sach cac phuong an tra loi vao co so du lieu:
 * - Bat buoc phai co it nhat 2 dap an va dung 1 dap an dung (is_correct = 1).
 * - Tham so answers_json la mot chuoi JSON mang (array), moi phan tu la mot
 *   object gom:
 *     "content": chuoi noi dung dap an (khong rong)
 *     "is_correct": so nguyen (0 hoac 1)
 *   Vi du:
 *     [
 *       {"content": "Dap an A", "is_correct": 1},
 *       {"content": "Dap an B", "is_correct": 0}
 *     ]
 * - Thao tac ghi duoc bao ve boi SQLite transaction (BEGIN IMMEDIATE).
 * - Khi tao thanh cong, ham tra ve QUESTION_SUCCESS va gan ID cua cau hoi vua
 *   tao vao *out_question_id (> 0).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - content: Noi dung cau hoi (khong duoc NULL hoac chuoi rong).
 * - topic: Chu de cua cau hoi (khong duoc NULL hoac chuoi rong).
 * - difficulty: Do kho 'EASY'|'MEDIUM'|'HARD' (khong duoc NULL).
 * - answers_json: Chuoi JSON mang danh sach dap an (khong duoc NULL).
 * - out_question_id: Con tro nhan ID cau hoi vua tao (khong duoc NULL).
 *
 * Hop dong bo nho va gia tri out:
 * - Khi thanh cong: *out_question_id nhan ID cua cau hoi moi (> 0).
 * - Khi that bai: Neu out_question_id != NULL thi *out_question_id duoc dat ve 0.
 *
 * Tra ve:
 * - QUESTION_SUCCESS (0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi cac ma loi QUESTION_ERR_* khi that bai.
 */
int question_create(sqlite3 *db,
                    const char *content,
                    const char *topic,
                    const char *difficulty,
                    const char *answers_json,
                    int *out_question_id);

/*
 * ============================================================================
 * API: question_list
 * ============================================================================
 *
 * Lay danh sach tom tat cac cau hoi trong he thong:
 * - Ho tro bo loc tuy chon theo topic va difficulty (truyen NULL neu khong loc).
 * - Ho tro phan trang thong qua limit va offset:
 *     limit > 0 (neu limit <= 0 hoac qua lon, co the ap dung gia tri mac dinh an toan).
 *     offset >= 0.
 * - Chuoi JSON ket qua tra ve qua *out_json chi chua thong tin khai quat cua cau hoi
 *   (id, content, topic, difficulty, created_at, answer_count) ma khong kem theo
 *   chi tiet cac dap an.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - topic: Chu de can loc (NULL neu muon lay tat ca).
 * - difficulty: Do kho can loc 'EASY'|'MEDIUM'|'HARD' (NULL neu muon lay tat ca).
 * - limit: So luong ban ghi toi da muon lay (> 0).
 * - offset: Vi tri bat dau lay (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach cau hoi (khong duoc NULL).
 *
 * Hop dong bo nho:
 * - Khi thanh cong: *out_json tro toi chuoi JSON duoc cap phat dong. Caller so huu
 *   chuoi nay va phai goi free() sau khi su dung xong.
 * - Khi that bai: Neu out_json != NULL thi *out_json duoc dat ve NULL.
 *
 * Tra ve:
 * - QUESTION_SUCCESS (0) khi thanh cong.
 * - Ma am (< 0) tuong ung voi cac ma loi QUESTION_ERR_* khi that bai.
 */
int question_list(sqlite3 *db,
                  const char *topic,
                  const char *difficulty,
                  int limit,
                  int offset,
                  char **out_json);

/*
 * ============================================================================
 * API: question_get_detail
 * ============================================================================
 *
 * Lay thong tin chi tiet day du cua mot cau hoi theo ID:
 * - Chuoi JSON ket qua bao gom: id, content, topic, difficulty, created_at va
 *   mang danh sach toan bo cac phuong an dap an kem theo co is_correct.
 * - Luu y bao mat: Du lieu tra ve day du ca dap an dung (is_correct) danh cho
 *   muc dich quan tri/quan ly cau hoi. Tang mang (Router/Gateway) phai kiem tra
 *   quyen nguoi dung (ADMIN/Giang vien) truoc khi cung cap du lieu nay cho client,
 *   tuyet doi khong tra ve cho thi sinh dang lam bai thi.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - question_id: ID cau hoi can xem chi tiet (> 0).
 * - out_json: Con tro nhan chuoi JSON chi tiet cau hoi (khong duoc NULL).
 *
 * Hop dong bo nho:
 * - Khi thanh cong: *out_json tro toi chuoi JSON duoc cap phat dong. Caller so huu
 *   chuoi nay va phai goi free() sau khi su dung xong.
 * - Khi that bai: Neu out_json != NULL thi *out_json duoc dat ve NULL.
 *
 * Tra ve:
 * - QUESTION_SUCCESS (0) khi thanh cong.
 * - QUESTION_ERR_NOT_FOUND (-3) neu question_id khong ton tai.
 * - Ma am (< 0) tuong ung voi cac ma loi QUESTION_ERR_* khac khi that bai.
 */
int question_get_detail(sqlite3 *db,
                        int question_id,
                        char **out_json);

/*
 * ============================================================================
 * API: question_update
 * ============================================================================
 *
 * Cap nhat noi dung cau hoi va tuy chon cap nhat danh sach dap an:
 * - Neu answers_json == NULL: Chi cap nhat thong tin co ban cua cau hoi (content,
 *   topic, difficulty).
 * - Neu answers_json != NULL: Cap nhat thong tin cau hoi va thay the toan bo dap
 *   an cu bang danh sach dap an moi (phai thoa man >= 2 dap an va dung 1 dap an dung).
 * - Bao ve tinh toan ven lich su thi: Neu cau hoi da duoc tham chieu boi bat ky
 *   ban ghi nao trong practice_questions, room_questions hoac attempt_answers,
 *   ham tu choi cap nhat va tra ve QUESTION_ERR_IN_USE de tranh lam sai lech ket qua.
 * - Toan bo qua trinh cap nhat duoc thuc hien trong SQLite transaction.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - question_id: ID cau hoi can cap nhat (> 0).
 * - content: Noi dung cau hoi moi (khong duoc NULL hoac chuoi rong).
 * - topic: Chu de moi (khong duoc NULL hoac chuoi rong).
 * - difficulty: Do kho moi 'EASY'|'MEDIUM'|'HARD' (khong duoc NULL).
 * - answers_json: Chuoi JSON danh sach dap an moi (co the NULL neu khong sua dap an).
 *
 * Tra ve:
 * - QUESTION_SUCCESS (0) khi thanh cong.
 * - QUESTION_ERR_NOT_FOUND (-3) neu question_id khong ton tai.
 * - QUESTION_ERR_IN_USE (-5) neu cau hoi da co trong bai thi/luyen tap.
 * - Ma am (< 0) tuong ung voi cac ma loi QUESTION_ERR_* khac khi that bai.
 */
int question_update(sqlite3 *db,
                    int question_id,
                    const char *content,
                    const char *topic,
                    const char *difficulty,
                    const char *answers_json);

/*
 * ============================================================================
 * API: question_delete
 * ============================================================================
 *
 * Xoa mot cau hoi va toan bo dap an lien quan khoi co so du lieu:
 * - Kiem tra rang buoc tham chieu truoc khi xoa: Neu cau hoi da tung duoc su dung
 *   trong practice_questions, room_questions hoac attempt_answers, ham tu choi
 *   xoa va tra ve QUESTION_ERR_IN_USE nham bao ve toan ven lich su thi cua thi sinh.
 * - Neu cau hoi chua tung duoc tham chieu: Thuc hien xoa toan bo cac phuong an dap an
 *   trong bang answers truoc, sau do xoa ban ghi cau hoi trong bang questions trong
 *   cung mot transaction (BEGIN IMMEDIATE).
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo (khong duoc NULL).
 * - question_id: ID cau hoi can xoa (> 0).
 *
 * Tra ve:
 * - QUESTION_SUCCESS (0) khi thanh cong.
 * - QUESTION_ERR_NOT_FOUND (-3) neu question_id khong ton tai.
 * - QUESTION_ERR_IN_USE (-5) neu cau hoi da duoc su dung trong he thong.
 * - Ma am (< 0) tuong ung voi cac ma loi QUESTION_ERR_* khac khi that bai.
 */
int question_delete(sqlite3 *db,
                    int question_id);

#endif /* QUESTION_H */

