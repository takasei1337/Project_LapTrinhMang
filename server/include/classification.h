#ifndef CLASSIFICATION_H
#define CLASSIFICATION_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Classification
 * ============================================================================
 */
#define CLASSIFICATION_SUCCESS             0  /* Thao tac thanh cong */
#define CLASSIFICATION_ERR_INVALID_PARAM  -1  /* Tham so dau vao khong hop le hoac con tro NULL */
#define CLASSIFICATION_ERR_DATABASE       -2  /* Loi truy van, prepare hoac ghi du lieu SQLite */
#define CLASSIFICATION_ERR_NOT_FOUND      -3  /* Ban ghi hoac danh muc cha (parent) khong ton tai */
#define CLASSIFICATION_ERR_DUPLICATE      -4  /* Vi pham rang buoc UNIQUE (code, name, order_index) */
#define CLASSIFICATION_ERR_IN_USE         -5  /* Ban ghi dang co danh muc con hoac cau hoi tham chieu (RESTRICT) */
#define CLASSIFICATION_ERR_MEMORY         -6  /* Loi cap phat bo nho dong */

/*
 * ============================================================================
 * SUBJECT APIs
 * ============================================================================
 */

/*
 * Tao mon hoc moi:
 * - code: Ma mon hoc duy nhat (khong duoc NULL hoac chuoi rong/khoang trang).
 * - name: Ten mon hoc (khong duoc NULL hoac chuoi rong/khoang trang).
 * - description: Mo ta mon hoc (co the NULL).
 * - out_subject_id: Con tro nhan ID mon hoc vua tao (> 0).
 */
int classification_subject_create(sqlite3 *db,
                                  const char *code,
                                  const char *name,
                                  const char *description,
                                  int *out_subject_id);

/*
 * Lay danh sach mon hoc kem phan trang va so luong chuong (chapter_count):
 * - limit: So luong ban ghi toi da (mac dinh 20 neu <= 0).
 * - offset: Vi tri bat dau lay (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach. Caller co trach nhiem goi free().
 */
int classification_subject_list(sqlite3 *db,
                                int limit,
                                int offset,
                                char **out_json);

/*
 * Lay chi tiet mot mon hoc:
 * - subject_id: ID mon hoc (> 0).
 * - out_json: Con tro nhan chuoi JSON chi tiet. Caller co trach nhiem goi free().
 */
int classification_subject_get(sqlite3 *db,
                               int subject_id,
                               char **out_json);

/*
 * Cap nhat mon hoc:
 * - code: Ma mon moi (neu NULL giu nguyen ma cu).
 * - name: Ten mon moi (neu NULL giu nguyen ten cu).
 * - description: Mo ta moi (neu NULL giu nguyen mo ta cu).
 * - It nhat mot truong phai duoc cung cap de cap nhat.
 */
int classification_subject_update(sqlite3 *db,
                                  int subject_id,
                                  const char *code,
                                  const char *name,
                                  const char *description);

/*
 * Xoa mon hoc:
 * - subject_id: ID mon hoc can xoa.
 * - Tu choi xoa va tra ve CLASSIFICATION_ERR_IN_USE neu con chuong thuoc mon hoc nay (RESTRICT).
 */
int classification_subject_delete(sqlite3 *db,
                                  int subject_id);

/*
 * ============================================================================
 * CHAPTER APIs
 * ============================================================================
 */

/*
 * Tao chuong moi:
 * - subject_id: ID mon hoc cha (> 0, phai ton tai trong subjects).
 * - order_index: So thu tu chuong trong mon (> 0, duy nhat trong subject_id).
 * - name: Ten chuong (khong rong, duy nhat trong subject_id).
 * - description: Mo ta chuong (co the NULL).
 * - out_chapter_id: Con tro nhan ID chuong vua tao (> 0).
 */
int classification_chapter_create(sqlite3 *db,
                                  int subject_id,
                                  int order_index,
                                  const char *name,
                                  const char *description,
                                  int *out_chapter_id);

/*
 * Lay danh sach chuong (loc theo subject_id neu > 0) kem phan trang va so luong bai (lesson_count):
 * - subject_id: ID mon hoc can loc (neu <= 0 lay tat ca chuong).
 * - limit: So luong toi da (mac dinh 20 neu <= 0).
 * - offset: Vi tri bat dau (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach. Caller co trach nhiem goi free().
 */
int classification_chapter_list(sqlite3 *db,
                                int subject_id,
                                int limit,
                                int offset,
                                char **out_json);

/*
 * Lay chi tiet mot chuong:
 * - chapter_id: ID chuong (> 0).
 * - out_json: Con tro nhan chuoi JSON chi tiet. Caller co trach nhiem goi free().
 */
int classification_chapter_get(sqlite3 *db,
                               int chapter_id,
                               char **out_json);

/*
 * Cap nhat chuong:
 * - order_index: So thu tu moi (neu <= 0 giu nguyen so thu tu cu).
 * - name: Ten chuong moi (neu NULL giu nguyen ten cu).
 * - description: Mo ta moi (neu NULL giu nguyen mo ta cu).
 * - It nhat mot truong phai duoc cung cap de cap nhat.
 */
int classification_chapter_update(sqlite3 *db,
                                  int chapter_id,
                                  int order_index,
                                  const char *name,
                                  const char *description);

/*
 * Xoa chuong:
 * - chapter_id: ID chuong can xoa.
 * - Tu choi xoa va tra ve CLASSIFICATION_ERR_IN_USE neu con bai hoc thuoc chuong (RESTRICT).
 */
int classification_chapter_delete(sqlite3 *db,
                                  int chapter_id);

/*
 * ============================================================================
 * LESSON APIs
 * ============================================================================
 */

/*
 * Tao bai hoc moi:
 * - chapter_id: ID chuong cha (> 0, phai ton tai trong chapters).
 * - order_index: So thu tu bai trong chuong (> 0, duy nhat trong chapter_id).
 * - name: Ten bai hoc (khong rong, duy nhat trong chapter_id).
 * - description: Mo ta bai hoc (co the NULL).
 * - out_lesson_id: Con tro nhan ID bai hoc vua tao (> 0).
 */
int classification_lesson_create(sqlite3 *db,
                                 int chapter_id,
                                 int order_index,
                                 const char *name,
                                 const char *description,
                                 int *out_lesson_id);

/*
 * Lay danh sach bai hoc (loc theo chapter_id neu > 0) kem phan trang va so luong topic (topic_count):
 * - chapter_id: ID chuong can loc (neu <= 0 lay tat ca bai).
 * - limit: So luong toi da (mac dinh 20 neu <= 0).
 * - offset: Vi tri bat dau (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach. Caller co trach nhiem goi free().
 */
int classification_lesson_list(sqlite3 *db,
                               int chapter_id,
                               int limit,
                               int offset,
                               char **out_json);

/*
 * Lay chi tiet mot bai hoc:
 * - lesson_id: ID bai hoc (> 0).
 * - out_json: Con tro nhan chuoi JSON chi tiet. Caller co trach nhiem goi free().
 */
int classification_lesson_get(sqlite3 *db,
                              int lesson_id,
                              char **out_json);

/*
 * Cap nhat bai hoc:
 * - order_index: So thu tu moi (neu <= 0 giu nguyen so thu tu cu).
 * - name: Ten bai moi (neu NULL giu nguyen ten cu).
 * - description: Mo ta moi (neu NULL giu nguyen mo ta cu).
 * - It nhat mot truong phai duoc cung cap de cap nhat.
 */
int classification_lesson_update(sqlite3 *db,
                                 int lesson_id,
                                 int order_index,
                                 const char *name,
                                 const char *description);

/*
 * Xoa bai hoc:
 * - lesson_id: ID bai hoc can xoa.
 * - Tu choi xoa va tra ve CLASSIFICATION_ERR_IN_USE neu con chu de (topic) thuoc bai hoc (RESTRICT).
 */
int classification_lesson_delete(sqlite3 *db,
                                 int lesson_id);

/*
 * ============================================================================
 * TOPIC APIs
 * ============================================================================
 */

/*
 * Tao chu de (topic) moi:
 * - lesson_id: ID bai hoc cha (> 0, phai ton tai trong lessons).
 * - name: Ten chu de (khong rong, duy nhat trong lesson_id).
 * - description: Mo ta chu de (co the NULL).
 * - out_topic_id: Con tro nhan ID chu de vua tao (> 0).
 */
int classification_topic_create(sqlite3 *db,
                                int lesson_id,
                                const char *name,
                                const char *description,
                                int *out_topic_id);

/*
 * Lay danh sach chu de (loc theo lesson_id neu > 0) kem phan trang va so luong cau hoi (question_count):
 * - lesson_id: ID bai hoc can loc (neu <= 0 lay tat ca topic).
 * - limit: So luong toi da (mac dinh 20 neu <= 0).
 * - offset: Vi tri bat dau (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach. Caller co trach nhiem goi free().
 */
int classification_topic_list(sqlite3 *db,
                              int lesson_id,
                              int limit,
                              int offset,
                              char **out_json);

/*
 * Lay chi tiet mot chu de:
 * - topic_id: ID chu de (> 0).
 * - out_json: Con tro nhan chuoi JSON chi tiet. Caller co trach nhiem goi free().
 */
int classification_topic_get(sqlite3 *db,
                             int topic_id,
                             char **out_json);

/*
 * Cap nhat chu de:
 * - name: Ten chu de moi (neu NULL giu nguyen ten cu).
 * - description: Mo ta moi (neu NULL giu nguyen mo ta cu).
 * - It nhat mot truong phai duoc cung cap de cap nhat.
 */
int classification_topic_update(sqlite3 *db,
                                int topic_id,
                                const char *name,
                                const char *description);

/*
 * Xoa chu de:
 * - topic_id: ID chu de can xoa.
 * - Tu choi xoa va tra ve CLASSIFICATION_ERR_IN_USE neu co cau hoi (questions.topic_id)
 *   dang tham chieu toi chu de nay (RESTRICT).
 */
int classification_topic_delete(sqlite3 *db,
                                int topic_id);

#endif /* CLASSIFICATION_H */

