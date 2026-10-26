
# Hướng dẫn công việc — Tuấn Anh

## 1. Nhiệm vụ

Bạn phụ trách các chức năng:

- Xây dựng TCP Socket Server.
- Xử lý luồng dữ liệu TCP và nhiều client.
- Tạo phòng thi.
- Xem danh sách phòng thi.
- Tham gia phòng thi.
- Định tuyến request đến các module nghiệp vụ.
- Tích hợp gateway để giao diện web kết nối với server.

## 2. Các file cần đọc

Trước khi viết code, đọc theo thứ tự:

1. `README.md` — Tổng quan dự án.
2. `docs/protocol.md` — Giao thức truyền dữ liệu.
3. `server/include/protocol.h` — Các loại request và thông số mạng.
4. `server/include/room.h` — Các hàm quản lý phòng.
5. `database/schema.sql` — Cấu trúc cơ sở dữ liệu.

## 3. Phạm vi code

Bạn phụ trách:

- `server/src/main.c`
- `server/src/network/`
- `server/src/room/`

Không tự ý thay đổi các module của thành viên khác.

## 4. Thứ tự triển khai

### Giai đoạn 1 — TCP Server

- Hoàn thiện `socket()`, `bind()`, `listen()` và `accept()`.
- Sử dụng `poll()` để xử lý nhiều client.
- Xử lý client ngắt kết nối.
- Giải phóng tài nguyên khi server dừng.

### Giai đoạn 2 — Truyền dữ liệu

- Đọc đủ 4 byte header chứa độ dài JSON.
- Chuyển độ dài từ network byte order.
- Kiểm tra giới hạn kích thước message.
- Đọc đủ JSON payload.
- Xử lý trường hợp TCP chia hoặc gộp message.
- Gửi response theo cùng định dạng.

### Giai đoạn 3 — Quản lý phòng

Triển khai các hàm trong `server/include/room.h`:

- `room_create()`
- `room_list()`
- `room_join()`

Sử dụng các bảng `rooms`, `room_members` và `room_questions`.

### Giai đoạn 4 — Tích hợp

- Phân tích trường `type` của request.
- Gọi hàm nghiệp vụ tương ứng.
- Trả response theo `docs/protocol.md`.
- Phối hợp với Công Minh để kết nối giao diện web.

## 5. Quy tắc quan trọng

- Không xử lý trực tiếp nghiệp vụ tài khoản hoặc chấm điểm trong code network.
- Không giả định một lần `recv()` sẽ nhận đủ một message.
- Không gửi trực tiếp cấu trúc C qua TCP.
- Kiểm tra lỗi của các lời gọi socket.
- Không sử dụng dữ liệu từ client khi chưa kiểm tra hợp lệ.
- Comment trong code viết tiếng Việt không dấu.
- Tài liệu Markdown viết tiếng Việt có dấu.

## 6. Kiểm tra

Trước khi tạo pull request:

- Chạy `make clean && make`.
- Kiểm tra server có thể nhận kết nối.
- Kiểm tra nhiều client kết nối đồng thời.
- Kiểm tra message bị chia thành nhiều lần gửi.
- Kiểm tra request không hợp lệ không làm server bị crash.

## 7. Lưu ý

Không cần tự thiết kế lại giao thức hoặc database.

Nếu cần thay đổi header dùng chung, giao thức hoặc schema, phải trao đổi với nhóm trước.


## Quy ước tích hợp Network với module Exam

Tuấn Anh phụ trách nhận request từ client, kiểm tra cấu trúc JSON và chuyển đến các hàm nghiệp vụ của Công Minh.

### Định tuyến request bài thi

| Request | Hàm nghiệp vụ |
|---|---|
| `EXAM_START` | `exam_start(db, room_id, user_id)` |
| `EXAM_ANSWER` | `exam_answer(db, attempt_id, user_id, question_id, answer_id)` |
| `EXAM_SUBMIT` | `exam_submit(db, attempt_id, user_id)` |
| `EXAM_RESULT` | `exam_result(db, attempt_id, user_id)` |
| `EXAM_HISTORY` | `exam_history(db, user_id)` |

Các hàm được khai báo trong `server/include/exam.h`.

### Trách nhiệm của Network

1. Nhận đầy đủ thông điệp TCP theo quy tắc đóng khung 4 byte.
2. Phân tích JSON bằng cJSON.
3. Xác định loại request qua trường `type`.
4. Kiểm tra phiên đăng nhập và xác định `user_id`.
5. Đọc, kiểm tra các tham số trong `payload`.
6. Gọi hàm nghiệp vụ tương ứng.
7. Đóng gói kết quả thành response JSON theo `docs/protocol.md`.
8. Gửi response về client.

Không lấy `user_id` trực tiếp từ `payload` để xác định người thực hiện thao tác.

### Lưu đáp án ngay khi chọn

Mỗi request `EXAM_ANSWER` tương ứng với một lần người dùng chọn hoặc thay đổi đáp án.

Network phải chuyển request đến `exam_answer()` và chỉ trả về `saved: true` khi thao tác lưu thành công.

Không tự lưu đáp án trong module Network.

### Phân chia trách nhiệm

- **Tuấn Anh:** TCP, đóng khung thông điệp, JSON, định tuyến, gateway và quản lý phòng.
- **Công Minh:** Xử lý lượt thi, lưu đáp án, chấm điểm, kết quả và lịch sử.
- **Thế Anh:** Xác thực, phân quyền, luyện tập, câu hỏi và nhật ký hoạt động.

Không triển khai trùng chức năng giữa các module.

### Tài liệu cần đối chiếu

- `docs/protocol.md`, mục 11: API bài thi.
- `server/include/protocol.h`: Tên request.
- `server/include/exam.h`: Chữ ký hàm nghiệp vụ.
- `database/schema.sql`: Các bảng lưu dữ liệu thi.
