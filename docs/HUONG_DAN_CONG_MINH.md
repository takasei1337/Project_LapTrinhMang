
# Hướng dẫn công việc — Công Minh

## 1. Nhiệm vụ

Bạn phụ trách:

- Xây dựng giao diện người dùng.
- Hiển thị danh sách phòng thi và giao diện làm bài.
- Bắt đầu bài thi.
- Chọn và thay đổi đáp án.
- Nộp bài và chấm điểm.
- Hiển thị kết quả phòng thi.
- Hiển thị lịch sử thi và biểu đồ thống kê.

## 2. Các file cần đọc

Trước khi viết code, đọc theo thứ tự:

1. `README.md` — Tổng quan dự án và phân công.
2. `docs/protocol.md` — Quy ước gửi và nhận dữ liệu.
3. `server/include/protocol.h` — Danh sách request.
4. `server/include/exam.h` — Các hàm xử lý bài thi.
5. `database/schema.sql` — Cấu trúc cơ sở dữ liệu.

## 3. Phạm vi code

Bạn phụ trách:

- `client/` — Giao diện HTML, CSS và JavaScript.
- `server/src/exam/` — Xử lý nghiệp vụ thi và chấm điểm.

Không tự ý chỉnh sửa module của thành viên khác.

## 4. Thứ tự triển khai

### Giai đoạn 1 — Giao diện

Tạo các màn hình:

1. Đăng ký và đăng nhập.
2. Danh sách phòng thi.
3. Tạo phòng và tham gia phòng.
4. Làm bài thi.
5. Kết quả bài thi.
6. Lịch sử thi và biểu đồ.
7. Luyện tập trắc nghiệm.

Có thể sử dụng dữ liệu mẫu để dựng giao diện trước khi backend hoàn thiện.

### Giai đoạn 2 — Xử lý bài thi

Triển khai các hàm trong `server/include/exam.h`:

- `exam_start()` — Bắt đầu bài thi.
- `exam_answer()` — Lưu hoặc thay đổi đáp án.
- `exam_submit()` — Nộp bài và chấm điểm.
- `exam_result()` — Lấy kết quả bài thi.
- `exam_history()` — Lấy lịch sử thi.

Sử dụng các bảng:

- `rooms`
- `room_questions`
- `questions`
- `answers`
- `exam_attempts`
- `attempt_answers`

### Giai đoạn 3 — Quy tắc xử lý bài thi

- Chỉ người có quyền mới được bắt đầu bài thi.
- Mỗi thí sinh chỉ được làm bài theo trạng thái phòng hợp lệ.
- Đáp án có thể thay đổi trước khi nộp bài hoặc hết giờ.
- Sau khi nộp bài, không được sửa đáp án.
- Server chịu trách nhiệm tính điểm.
- Không gửi đáp án đúng cho client khi bài thi đang diễn ra.
- Kiểm tra thời gian làm bài ở server, không chỉ dựa vào đồng hồ trên giao diện.

### Giai đoạn 4 — Tích hợp giao diện

- JavaScript gửi request JSON theo `docs/protocol.md`.
- Không kết nối trực tiếp từ trình duyệt đến TCP Socket thuần.
- Phối hợp với Tuấn Anh để sử dụng HTTP/WebSocket gateway.
- Hiển thị thông báo khi request thất bại.
- Không tự ý đổi tên request hoặc cấu trúc response.

## 5. Kiểm tra

Trước khi tạo pull request:

- Chạy `make clean && make` để kiểm tra backend.
- Kiểm tra chọn và thay đổi đáp án.
- Kiểm tra nộp bài trước thời hạn.
- Kiểm tra không thể sửa bài sau khi nộp.
- Kiểm tra kết quả chấm điểm.
- Kiểm tra giao diện hiển thị khi server trả lỗi.

## 6. Quy ước

- Comment trong code viết tiếng Việt không dấu.
- Tài liệu Markdown viết tiếng Việt có dấu.
- Không sửa các file dùng chung khi chưa trao đổi với nhóm.
- Không tự tạo giao thức hoặc database riêng.

## 7. Lưu ý

Bạn có thể triển khai giao diện với dữ liệu mẫu trước.

Khi backend và gateway sẵn sàng, thay dữ liệu mẫu bằng request thực tế theo giao thức chung.


## Quy ước tích hợp module Exam

Các hàm nghiệp vụ được khai báo tại `server/include/exam.h`.

| Hàm | Chức năng |
|---|---|
| `exam_start(db, room_id, user_id)` | Tạo lượt thi mới, trả về `attempt_id` |
| `exam_answer(db, attempt_id, user_id, question_id, answer_id)` | Lưu hoặc thay đổi đáp án |
| `exam_submit(db, attempt_id, user_id)` | Nộp bài và chấm điểm |
| `exam_result(db, attempt_id, user_id)` | Lấy kết quả lượt thi |
| `exam_history(db, user_id)` | Lấy lịch sử thi |

### Quy tắc lưu đáp án

**Mỗi lần người dùng chọn hoặc thay đổi đáp án, giao diện phải gửi ngay request `EXAM_ANSWER`.**

Ví dụ: Người dùng chọn đáp án A, sau đó đổi sang C.

1. Giao diện gửi request lưu đáp án A.
2. Người dùng đổi sang C.
3. Giao diện gửi request lưu đáp án C.
4. Server cập nhật lựa chọn mới nhất thành C.
5. Khi nộp bài, server chấm điểm dựa trên đáp án đã lưu.

Không chờ đến lúc nộp bài mới gửi toàn bộ đáp án.

Giao diện chỉ hiển thị trạng thái **đã lưu** sau khi nhận được response thành công từ server.

### Quy tắc chấm điểm

- Server chịu trách nhiệm chấm điểm.
- Mỗi câu đúng được 1 điểm thô.
- Câu sai hoặc chưa trả lời được 0 điểm.
- Điểm thang 10: `correct_count * 10.0 / total_questions`.
- Không hiển thị đáp án đúng trong lúc bài thi đang diễn ra.
- Sau khi nộp bài, không cho phép thay đổi đáp án.

### Tài liệu bắt buộc đọc

Xem mục **11. API làm bài thi và kết quả** trong `docs/protocol.md` để biết cấu trúc JSON của từng request và response.

Không tự ý thay đổi tên trường JSON hoặc chữ ký hàm trong `exam.h` khi chưa thống nhất với các thành viên.
