#ifndef EXAM_H
#define EXAM_H

#include <sqlite3.h>

/* Bat dau luot thi trong phong, tra ve attempt_id */
int exam_start(sqlite3 *db, int room_id, int user_id);

/* Luu hoac cap nhat dap an cua mot cau hoi */
int exam_answer(sqlite3 *db, int attempt_id, int user_id,
                int question_id, int answer_id);

/* Nop bai va cham diem theo cac dap an da luu */
int exam_submit(sqlite3 *db, int attempt_id, int user_id);

/* Lay ket qua cua mot luot thi da hoan thanh */
/* Chuoi JSON tra ve duoc cap phat dong, ben goi phai free() */
char *exam_result(sqlite3 *db, int attempt_id, int user_id);

/* Lay lich su thi cua nguoi dung */
/* Chuoi JSON tra ve duoc cap phat dong, ben goi phai free() */
char *exam_history(sqlite3 *db, int user_id);

#endif
