#ifndef QUESTION_CLASSIFICATION_H
#define QUESTION_CLASSIFICATION_H

#include <sqlite3.h>

/*
 * ============================================================================
 * Ma trang thai tra ve cua module Question Classification Mapping
 * ============================================================================
 */
#define QUESTION_CLASSIFICATION_SUCCESS             0  /* Thao tac thanh cong */
#define QUESTION_CLASSIFICATION_ERR_INVALID_PARAM  -1  /* Tham so dau vao khong hop le hoac con tro NULL */
#define QUESTION_CLASSIFICATION_ERR_DATABASE       -2  /* Loi truy van, prepare hoac ghi du lieu SQLite */
#define QUESTION_CLASSIFICATION_ERR_NOT_FOUND      -3  /* Cau hoi hoac topic khong ton tai */
#define QUESTION_CLASSIFICATION_ERR_MEMORY         -4  /* Loi cap phat bo nho dong */

/*
 * ============================================================================
 * API: question_classification_assign
 * ============================================================================
 * Gán câu hỏi vào Topic theo ID tường minh:
 * - Kiểm tra Question và Topic tồn tại trong database.
 * - Cập nhật questions.topic_id = topic_id.
 * - Cho phép chuyển câu hỏi từ Topic này sang Topic khác.
 * - Tuyệt đối không thay đổi chuỗi questions.topic cũ.
 *
 * Tham số:
 * - db: Con trỏ kết nối SQLite3 đang mở.
 * - question_id: ID câu hỏi (> 0).
 * - topic_id: ID chủ đề (> 0).
 *
 * Trả về:
 * - QUESTION_CLASSIFICATION_SUCCESS (0) khi thành công.
 * - QUESTION_CLASSIFICATION_ERR_NOT_FOUND (-3) nếu question_id hoặc topic_id không tồn tại.
 * - Mã âm khác tương ứng khi thất bại.
 */
int question_classification_assign(sqlite3 *db,
                                   int question_id,
                                   int topic_id);

/*
 * ============================================================================
 * API: question_classification_unassign
 * ============================================================================
 * Gỡ câu hỏi khỏi cây phân loại:
 * - Đặt questions.topic_id = NULL.
 * - Không xóa câu hỏi, không làm mất đáp án hay lịch sử thi.
 * - Không xóa hoặc thay đổi chuỗi questions.topic cũ.
 *
 * Tham số:
 * - db: Con trỏ kết nối SQLite3 đang mở.
 * - question_id: ID câu hỏi (> 0).
 *
 * Trả về:
 * - QUESTION_CLASSIFICATION_SUCCESS (0) khi thành công.
 * - QUESTION_CLASSIFICATION_ERR_NOT_FOUND (-3) nếu question_id không tồn tại.
 * - Mã âm khác tương ứng khi thất bại.
 */
int question_classification_unassign(sqlite3 *db,
                                     int question_id);

/*
 * ============================================================================
 * API: question_classification_get
 * ============================================================================
 * Lấy thông tin phân loại hiện tại của câu hỏi theo ID:
 * - Chuỗi JSON kết quả bao gồm: question_id, content, topic (chuỗi cũ),
 *   difficulty, created_at, topic_id.
 * - Nếu câu hỏi đã phân loại: bao gồm đầy đủ subject, chapter, lesson, topic.
 * - Nếu chưa phân loại: thể hiện rõ topic_id = null và các trường phân cấp là null.
 *
 * Tham số:
 * - db: Con trỏ kết nối SQLite3 đang mở.
 * - question_id: ID câu hỏi (> 0).
 * - out_json: Con trỏ nhận chuỗi JSON kết quả. Caller sở hữu và chịu trách nhiệm gọi free().
 *
 * Trả về:
 * - QUESTION_CLASSIFICATION_SUCCESS (0) khi thành công.
 * - QUESTION_CLASSIFICATION_ERR_NOT_FOUND (-3) nếu question_id không tồn tại.
 * - Mã âm khác tương ứng khi thất bại.
 */
int question_classification_get(sqlite3 *db,
                                int question_id,
                                char **out_json);

/*
 * ============================================================================
 * API: question_classification_list
 * ============================================================================
 * Lọc danh sách câu hỏi đã phân loại qua cây phân cấp:
 * - JOIN qua subjects, chapters, lessons, topics.
 * - Hỗ trợ lọc theo subject_id, chapter_id, lesson_id, topic_id (<= 0 bỏ qua bộ lọc đó).
 * - Hỗ trợ lọc theo difficulty ('EASY'|'MEDIUM'|'HARD' hoặc NULL/rỗng nếu không lọc).
 * - Hỗ trợ phân trang limit/offset và tính tổng số bản ghi (total).
 * - Nếu nhiều bộ lọc mâu thuẫn (ví dụ topic thuộc lesson khác), trả về danh sách rỗng (count=0).
 * - Tuyệt đối không trả về đáp án đúng trong danh sách tóm tắt.
 *
 * Tham số:
 * - db: Con trỏ kết nối SQLite3 đang mở.
 * - subject_id: ID môn học can loc (<= 0 neu khong loc).
 * - chapter_id: ID chuong can loc (<= 0 neu khong loc).
 * - lesson_id: ID bai hoc can loc (<= 0 neu khong loc).
 * - topic_id: ID chu de can loc (<= 0 neu khong loc).
 * - difficulty: Do kho can loc (NULL hoac rong neu khong loc).
 * - limit: So luong toi da (> 0, mac dinh 20 neu <= 0).
 * - offset: Vi tri bat dau (>= 0).
 * - out_json: Con tro nhan chuoi JSON danh sach. Caller so huu va phai free().
 *
 * Trả về:
 * - QUESTION_CLASSIFICATION_SUCCESS (0) khi thành công.
 * - QUESTION_CLASSIFICATION_ERR_INVALID_PARAM (-1) nếu tham số không hợp lệ.
 * - Mã âm khác khi thất bại.
 */
int question_classification_list(sqlite3 *db,
                                 int subject_id,
                                 int chapter_id,
                                 int lesson_id,
                                 int topic_id,
                                 const char *difficulty,
                                 int limit,
                                 int offset,
                                 char **out_json);

/*
 * ============================================================================
 * API: question_classification_list_unassigned
 * ============================================================================
 * Liệt kê các câu hỏi chưa được phân loại (questions.topic_id IS NULL):
 * - Phục vụ luồng phân loại thủ công dữ liệu câu hỏi cũ.
 * - Hỗ trợ phân trang limit/offset và tính tổng số câu hỏi chưa gán topic.
 *
 * Tham số:
 * - db: Con trỏ kết nối SQLite3 đang mở.
 * - limit: Số lượng tối đa (> 0, mặc định 20 nếu <= 0).
 * - offset: Vị trí bắt đầu (>= 0).
 * - out_json: Con trỏ nhận chuỗi JSON danh sách. Caller sở hữu và phải free().
 *
 * Trả về:
 * - QUESTION_CLASSIFICATION_SUCCESS (0) khi thành công.
 * - QUESTION_CLASSIFICATION_ERR_INVALID_PARAM (-1) nếu db hoặc out_json NULL.
 * - Mã âm khác khi thất bại.
 */
int question_classification_list_unassigned(sqlite3 *db,
                                            int limit,
                                            int offset,
                                            char **out_json);

#endif /* QUESTION_CLASSIFICATION_H */

