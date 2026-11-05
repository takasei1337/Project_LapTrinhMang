#ifndef QUESTION_SELECTOR_H
#define QUESTION_SELECTOR_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Balanced Question Selector
 * ============================================================================
 */
#define QUESTION_SELECTOR_SUCCESS             0  /* Chon cau hoi thanh cong */
#define QUESTION_SELECTOR_ERR_INVALID_PARAM  -1  /* Tham so khong hop le hoac con tro NULL */
#define QUESTION_SELECTOR_ERR_DATABASE       -2  /* Loi truy van SQLite */
#define QUESTION_SELECTOR_ERR_NOT_FOUND      -3  /* Bo loc Subject/Chapter/Lesson khong ton tai */
#define QUESTION_SELECTOR_ERR_MEMORY         -4  /* Loi cap phat bo nho dong */

/*
 * ============================================================================
 * API: question_selector_balanced
 * ============================================================================
 * Chon ngau nhien N cau hoi can bang giua cac Topic theo cay phan cap:
 * - Chi chon cau hoi co questions.topic_id IS NOT NULL.
 * - Chi chon cau hoi hop le: co it nhat 2 dap an va dung 1 dap an dung (is_correct = 1).
 * - Loc theo Subject, Chapter, Lesson va Difficulty (neu duoc chi dinh).
 * - Phan phoi quota cong bang theo Topic, ngan ngua lech tu:
 *     + Neu N >= K: phan phoi deu giua K Topic; phan du duoc chia ngau nhien.
 *     + Neu N < K: chon ngau nhien N Topic khac nhau, moi Topic 1 cau.
 *     + Neu Topic thieu nguon: tu dong chuyen phan thieu sang cac Topic con du.
 * - Neu tong nguon hop le < N: tra ve so cau thuc te va bao thieu hut trong shortage.
 * - Ngau nhien hoa ca thu tu chon Topic va thu tu cau hoi.
 * - Tuyet doi khong tra ve dap an dung hoac thong tin is_correct trong JSON.
 *
 * Tham so:
 * - db: Con tro ket noi SQLite3 dang mo.
 * - subject_id: ID mon hoc can loc (<= 0 neu khong loc).
 * - chapter_id: ID chuong can loc (<= 0 neu khong loc).
 * - lesson_id: ID bai hoc can loc (<= 0 neu khong loc).
 * - difficulty: Do kho can loc 'EASY'|'MEDIUM'|'HARD' (NULL hoac chuoi rong neu khong loc).
 * - num_questions: So luong cau hoi can chon (> 0).
 * - out_json: Con tro nhan chuoi JSON ket qua. Caller so huu va phai goi free().
 *
 * Tra ve:
 * - QUESTION_SELECTOR_SUCCESS (0) khi thanh cong.
 * - QUESTION_SELECTOR_ERR_INVALID_PARAM (-1) khi tham so khong hop le.
 * - Ma am khac khi that bai.
 */
int question_selector_balanced(sqlite3 *db,
                               int subject_id,
                               int chapter_id,
                               int lesson_id,
                               const char *difficulty,
                               int num_questions,
                               char **out_json);

/*
 * Phien ban mo rong bo sung them bo loc topic_id (<= 0 neu khong loc).
 */
int question_selector_balanced_ex(sqlite3 *db,
                                  int subject_id,
                                  int chapter_id,
                                  int lesson_id,
                                  int topic_id,
                                  const char *difficulty,
                                  int num_questions,
                                  char **out_json);

#endif /* QUESTION_SELECTOR_H */

