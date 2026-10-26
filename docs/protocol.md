
# Giao thức truyền thông — Ứng dụng thi trắc nghiệm trực tuyến

## 1. Kết nối

- Giao thức truyền tải: TCP.
- Cổng server: `8080`.
- Mã hóa văn bản: UTF-8.
- Định dạng dữ liệu: JSON.
- Thư viện xử lý JSON: cJSON.

Các chương trình client giao tiếp với TCP Server theo giao thức được mô tả trong tài liệu này.

**Lưu ý:** Trình duyệt web không hỗ trợ kết nối trực tiếp đến TCP Socket thuần. Giao diện HTML/CSS/JavaScript sẽ kết nối thông qua HTTP/WebSocket gateway do Tuấn Anh triển khai.

## 2. Đóng khung thông điệp TCP

Mỗi thông điệp có cấu trúc:

```text
[4 byte độ dài][JSON payload]
```

Trong đó:

- 4 byte đầu là số nguyên không dấu 32-bit.
- Thứ tự byte: network byte order (big-endian).
- Độ dài được tính bằng số byte JSON, không bao gồm header.
- Kích thước JSON tối đa: 1 MiB.
- JSON sử dụng UTF-8.

### 2.1. Quy tắc nhận dữ liệu

TCP là giao thức truyền luồng byte, không đảm bảo ranh giới thông điệp.

Vì vậy:

1. Đọc đủ 4 byte header.
2. Chuyển độ dài từ network byte order sang host byte order.
3. Kiểm tra độ dài hợp lệ và không vượt quá 1 MiB.
4. Đọc đủ số byte JSON đã khai báo.
5. Phân tích JSON bằng cJSON.
6. Tiếp tục xử lý các thông điệp còn lại trong luồng dữ liệu.

Không được giả định một lần `recv()` tương ứng với một thông điệp hoàn chỉnh.

### 2.2. Quy tắc gửi dữ liệu

1. Chuyển đối tượng JSON thành chuỗi UTF-8.
2. Tính số byte của chuỗi JSON.
3. Chuyển độ dài sang network byte order.
4. Gửi 4 byte header.
5. Gửi toàn bộ JSON payload.
6. Xử lý trường hợp `send()` chỉ gửi được một phần dữ liệu.

## 3. Cấu trúc request chung

Mọi request sử dụng cấu trúc:

```json
{
  "type": "AUTH_LOGIN",
  "request_id": "req_001",
  "payload": {
    "username": "user01",
    "password": "example"
  }
}
```

Ý nghĩa:

| Trường | Kiểu dữ liệu | Ý nghĩa |
|---|---|---|
| `type` | string | Tên thao tác |
| `request_id` | string | Mã định danh request |
| `payload` | object | Dữ liệu của thao tác |

Quy ước:

- `type` phải thuộc danh sách được khai báo trong `server/include/protocol.h`.
- `request_id` do client tạo và phải được giữ nguyên trong response.
- `payload` phải là đối tượng JSON, kể cả khi không có dữ liệu.

## 4. Response thành công

```json
{
  "type": "AUTH_LOGIN_RESULT",
  "request_id": "req_001",
  "success": true,
  "data": {
    "user_id": 1,
    "username": "user01",
    "role": "USER"
  },
  "error": null
}
```

Quy ước:

- `type` bằng tên request cộng hậu tố `_RESULT`.
- `request_id` giống request tương ứng.
- `success` bằng `true`.
- `data` chứa dữ liệu trả về.
- `error` bằng `null`.

## 5. Response thất bại

```json
{
  "type": "AUTH_LOGIN_RESULT",
  "request_id": "req_001",
  "success": false,
  "data": null,
  "error": {
    "code": "INVALID_CREDENTIALS",
    "message": "Sai tên đăng nhập hoặc mật khẩu"
  }
}
```

Quy ước:

- `success` bằng `false`.
- `data` bằng `null`.
- `error.code` là mã lỗi dành cho chương trình xử lý.
- `error.message` là thông báo dành cho người dùng.

Các lỗi chung:

| Mã lỗi | Ý nghĩa |
|---|---|
| `INVALID_REQUEST` | Request không đúng cấu trúc |
| `INVALID_PAYLOAD` | Dữ liệu gửi lên không hợp lệ |
| `UNKNOWN_REQUEST` | Loại request không được hỗ trợ |
| `UNAUTHORIZED` | Người dùng chưa đăng nhập |
| `FORBIDDEN` | Người dùng không có quyền |
| `DATABASE_ERROR` | Lỗi cơ sở dữ liệu |
| `INTERNAL_ERROR` | Lỗi xử lý phía server |

## 6. Xác thực và bảo mật

- Các thao tác yêu cầu đăng nhập phải được xác thực tại server.
- Server xác định `user_id` từ phiên đăng nhập đã xác thực.
- Không tin tưởng `user_id` hoặc `owner_id` do client tự gửi.
- Server kiểm tra quyền trước khi thực hiện thao tác.
- Không ghi mật khẩu hoặc đáp án đúng vào nhật ký.
- Không gửi đáp án đúng cho client trước khi bài thi được chấm.
- Không lưu mật khẩu dạng văn bản thuần trong database.
- Các thông tin nhạy cảm không được đưa vào thông báo lỗi.

**Cơ chế quản lý phiên và cách truyền thông tin xác thực sẽ được đặc tả riêng trước khi tích hợp chức năng đăng nhập.**

## 7. Danh sách request

Tên request được định nghĩa tại:

`server/include/protocol.h`

### 7.1. Tài khoản — Thế Anh

| Request | Chức năng |
|---|---|
| `AUTH_REGISTER` | Đăng ký tài khoản |
| `AUTH_LOGIN` | Đăng nhập |
| `AUTH_LOGOUT` | Đăng xuất |
| `AUTH_PROFILE` | Xem thông tin tài khoản |
| `AUTH_UPDATE` | Cập nhật tài khoản |

### 7.2. Luyện tập và câu hỏi — Thế Anh

| Request | Chức năng |
|---|---|
| `PRACTICE_START` | Bắt đầu luyện tập |
| `PRACTICE_SUBMIT` | Nộp bài luyện tập |
| `QUESTION_LIST` | Lấy danh sách câu hỏi |

### 7.3. Phòng thi — Tuấn Anh

| Request | Chức năng |
|---|---|
| `ROOM_CREATE` | Tạo phòng thi |
| `ROOM_LIST` | Xem danh sách phòng |
| `ROOM_JOIN` | Tham gia phòng |

### 7.4. Bài thi và kết quả — Công Minh

| Request | Chức năng |
|---|---|
| `EXAM_START` | Bắt đầu thi |
| `EXAM_ANSWER` | Chọn hoặc thay đổi đáp án |
| `EXAM_SUBMIT` | Nộp bài và chấm điểm |
| `EXAM_RESULT` | Xem kết quả |
| `EXAM_HISTORY` | Xem lịch sử thi |

## 8. Quy ước thời gian và trạng thái

### 8.1. Thời gian

Các mốc thời gian được truyền dưới dạng ISO 8601 UTC.

Ví dụ:

`2026-10-26T13:00:00Z`

Quy ước:

- Server là nguồn thời gian đáng tin cậy.
- Client có thể hiển thị thời gian theo múi giờ địa phương.
- Không sử dụng đồng hồ client để quyết định bài thi đã hết giờ hay chưa.
- Giá trị thời gian chưa được xác định sử dụng JSON `null`.

### 8.2. Trạng thái phòng

| Trạng thái | Ý nghĩa |
|---|---|
| `WAITING` | Phòng đang chờ bắt đầu |
| `IN_PROGRESS` | Phòng đang diễn ra |
| `FINISHED` | Phòng đã kết thúc |

## 9. API quản lý phòng thi

Người phụ trách: **Tuấn Anh**.

Các API trong mục này yêu cầu người dùng đã đăng nhập.

### 9.1. Tạo phòng thi — `ROOM_CREATE`

Request:

```json
{
  "type": "ROOM_CREATE",
  "request_id": "req_002",
  "payload": {
    "room_name": "Phòng thi Lập trình mạng",
    "question_count": 20,
    "duration_minutes": 30
  }
}
```

Ý nghĩa:

- `room_name`: Tên phòng thi, không được để trống.
- `question_count`: Số câu hỏi, phải là số nguyên dương.
- `duration_minutes`: Thời gian làm bài tính bằng phút, phải là số nguyên dương.
- `owner_id`: Server lấy từ phiên đăng nhập, không nhận trực tiếp từ client.

Server cần:

1. Kiểm tra người dùng đã đăng nhập.
2. Kiểm tra dữ liệu đầu vào.
3. Tạo bản ghi trong bảng `rooms`.
4. Đặt trạng thái ban đầu là `WAITING`.
5. Trả về ID của phòng vừa tạo.

Response thành công:

```json
{
  "type": "ROOM_CREATE_RESULT",
  "request_id": "req_002",
  "success": true,
  "data": {
    "room_id": 1,
    "status": "WAITING"
  },
  "error": null
}
```

Các mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `INVALID_PAYLOAD` | Thông tin phòng không hợp lệ |
| `DATABASE_ERROR` | Không thể tạo phòng |

**Lưu ý:** Cấu hình thời điểm bắt đầu và kết thúc phòng sẽ được bổ sung khi hoàn thiện quy tắc thời gian thi.

### 9.2. Xem danh sách phòng — `ROOM_LIST`

Request:

```json
{
  "type": "ROOM_LIST",
  "request_id": "req_003",
  "payload": {}
}
```

Response thành công:

```json
{
  "type": "ROOM_LIST_RESULT",
  "request_id": "req_003",
  "success": true,
  "data": {
    "rooms": [
      {
        "room_id": 1,
        "room_name": "Phòng thi Lập trình mạng",
        "owner_id": 2,
        "question_count": 20,
        "duration_minutes": 30,
        "start_time": null,
        "end_time": null,
        "status": "WAITING"
      }
    ]
  },
  "error": null
}
```

Quy ước:

- `rooms` là mảng chứa danh sách phòng thi.
- Nếu chưa có phòng, trả về `"rooms": []`.
- `status` chỉ nhận `WAITING`, `IN_PROGRESS` hoặc `FINISHED`.
- `start_time` và `end_time` sử dụng ISO 8601 UTC khi có giá trị.
- Không trả về thông tin nhạy cảm hoặc đáp án đúng.

Các mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `DATABASE_ERROR` | Không thể truy vấn danh sách phòng |

### 9.3. Tham gia phòng — `ROOM_JOIN`

Điều kiện:

- Người dùng đã đăng nhập.
- Phòng thi tồn tại.
- Phòng đang ở trạng thái `WAITING`.
- Người dùng chưa tham gia phòng.

Request:

```json
{
  "type": "ROOM_JOIN",
  "request_id": "req_004",
  "payload": {
    "room_id": 1
  }
}
```

Quy ước:

- `room_id` phải là số nguyên dương.
- `user_id` lấy từ phiên đăng nhập đã xác thực.
- Server thêm bản ghi vào bảng `room_members`.
- Không được thêm trùng người dùng vào cùng một phòng.

Response thành công:

```json
{
  "type": "ROOM_JOIN_RESULT",
  "request_id": "req_004",
  "success": true,
  "data": {
    "room_id": 1,
    "status": "WAITING",
    "joined": true
  },
  "error": null
}
```

Các mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `INVALID_PAYLOAD` | ID phòng không hợp lệ |
| `ROOM_NOT_FOUND` | Phòng không tồn tại |
| `ROOM_NOT_WAITING` | Phòng không còn nhận thành viên |
| `ALREADY_JOINED` | Đã tham gia phòng |
| `DATABASE_ERROR` | Không thể cập nhật cơ sở dữ liệu |

## 10. Quy định tích hợp giữa các thành viên

### Tuấn Anh — Network và Room

- Nhận và gửi dữ liệu qua TCP.
- Xử lý đóng khung thông điệp.
- Phân tích JSON và định tuyến request.
- Gọi các hàm nghiệp vụ tương ứng.
- Không tự triển khai lại chức năng đăng nhập hoặc chấm điểm.

### Công Minh — Exam và GUI

- Triển khai các hàm trong `server/include/exam.h`.
- Xây dựng giao diện dựa trên dữ liệu JSON.
- Không sử dụng raw TCP Socket trực tiếp từ trình duyệt.
- Phối hợp với Tuấn Anh để kết nối qua gateway.
- Không tự ý thay đổi cấu trúc request/response.

### Thế Anh — Auth, Practice, Question và Logger

- Triển khai các module tài khoản, luyện tập và câu hỏi.
- Cung cấp các hàm nghiệp vụ để Network gọi.
- Không xử lý trực tiếp `send()` hoặc `recv()` trong module nghiệp vụ.


## 11. API làm bài thi và kết quả

**Người phụ trách nghiệp vụ:** Công Minh.

**Người phụ trách truyền thông và định tuyến:** Tuấn Anh.

### 11.1. Quy tắc chung

- Người dùng phải đăng nhập trước khi làm bài.
- Server xác định `user_id` từ phiên đăng nhập đã xác thực.
- Một lượt thi được định danh bằng `attempt_id`.
- Người dùng chỉ được thao tác trên lượt thi của chính mình.
- Server quyết định thời gian bắt đầu, thời gian kết thúc và điểm số.
- Mỗi lần chọn hoặc thay đổi đáp án, client gửi ngay `EXAM_ANSWER`.
- Server lưu lựa chọn mới nhất của từng câu hỏi.
- Client không được nhận `is_correct` hoặc đáp án đúng khi bài thi đang diễn ra.
- Sau khi nộp bài, không được thay đổi đáp án.

### 11.2. Bắt đầu bài thi — `EXAM_START`

Request:

```json
{
  "type": "EXAM_START",
  "request_id": "req_005",
  "payload": {
    "room_id": 1
  }
}
```

Điều kiện:

- Phòng thi tồn tại.
- Người dùng là thành viên của phòng.
- Phòng cho phép bắt đầu làm bài.
- Người dùng chưa có lượt thi đang diễn ra trong phòng đó.

Response thành công:

```json
{
  "type": "EXAM_START_RESULT",
  "request_id": "req_005",
  "success": true,
  "data": {
    "attempt_id": 10,
    "room_id": 1,
    "duration_minutes": 30,
    "started_at": "2026-10-26T13:00:00Z",
    "deadline": "2026-10-26T13:30:00Z",
    "questions": [
      {
        "question_id": 101,
        "content": "Giao thức nào hoạt động ở tầng vận chuyển?",
        "topic": "Mạng máy tính",
        "difficulty": "EASY",
        "answers": [
          {
            "answer_id": 1001,
            "content": "TCP"
          },
          {
            "answer_id": 1002,
            "content": "IP"
          },
          {
            "answer_id": 1003,
            "content": "ARP"
          },
          {
            "answer_id": 1004,
            "content": "ICMP"
          }
        ]
      }
    ]
  },
  "error": null
}
```

Quy ước:

- `attempt_id` là ID của lượt thi trong bảng `exam_attempts`.
- `questions` chứa các câu hỏi đã được chọn cho phòng.
- Các câu hỏi trong cùng một phòng phải lấy từ bộ câu hỏi cố định tại bảng `room_questions`.
- Không trả về trường `is_correct` trong danh sách đáp án.
- `deadline` là thời hạn nộp bài do server xác định.
- Ví dụ trên chỉ minh họa một câu hỏi; số câu thực tế phụ thuộc cấu hình phòng.

Mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `ROOM_NOT_FOUND` | Phòng không tồn tại |
| `NOT_ROOM_MEMBER` | Chưa tham gia phòng |
| `EXAM_NOT_AVAILABLE` | Phòng chưa cho phép bắt đầu thi |
| `ATTEMPT_ALREADY_STARTED` | Đã có lượt thi đang diễn ra |
| `DATABASE_ERROR` | Lỗi cơ sở dữ liệu |

### 11.3. Lưu hoặc thay đổi đáp án — `EXAM_ANSWER`

**Quy tắc đã thống nhất: Lưu từng đáp án ngay khi chọn.**

Request:

```json
{
  "type": "EXAM_ANSWER",
  "request_id": "req_006",
  "payload": {
    "attempt_id": 10,
    "question_id": 101,
    "answer_id": 1003
  }
}
```

Server cần:

1. Xác thực người dùng.
2. Kiểm tra lượt thi thuộc về người dùng.
3. Kiểm tra lượt thi chưa nộp và chưa hết thời gian.
4. Kiểm tra câu hỏi thuộc bộ câu hỏi của phòng.
5. Kiểm tra đáp án thuộc câu hỏi đó.
6. Thêm mới hoặc cập nhật bản ghi trong `attempt_answers`.
7. Trả về xác nhận đã lưu.

Nếu người dùng đổi đáp án, server **cập nhật lựa chọn cũ**, không tạo thêm một lựa chọn thứ hai cho cùng câu hỏi.

Response thành công:

```json
{
  "type": "EXAM_ANSWER_RESULT",
  "request_id": "req_006",
  "success": true,
  "data": {
    "attempt_id": 10,
    "question_id": 101,
    "answer_id": 1003,
    "saved": true
  },
  "error": null
}
```

Mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `ATTEMPT_NOT_FOUND` | Lượt thi không tồn tại hoặc không thuộc người dùng |
| `EXAM_ALREADY_SUBMITTED` | Bài thi đã nộp |
| `EXAM_TIME_EXPIRED` | Đã hết thời gian làm bài |
| `QUESTION_NOT_IN_EXAM` | Câu hỏi không thuộc bài thi |
| `ANSWER_NOT_IN_QUESTION` | Đáp án không thuộc câu hỏi |
| `INVALID_PAYLOAD` | Dữ liệu không hợp lệ |
| `DATABASE_ERROR` | Lỗi lưu đáp án |

### 11.4. Nộp bài và chấm điểm — `EXAM_SUBMIT`

Request:

```json
{
  "type": "EXAM_SUBMIT",
  "request_id": "req_007",
  "payload": {
    "attempt_id": 10
  }
}
```

Server cần:

1. Xác thực người dùng và quyền sở hữu lượt thi.
2. Đọc các đáp án đã lưu trong `attempt_answers`.
3. Đối chiếu với đáp án đúng trong bảng `answers`.
4. Tính điểm và cập nhật `exam_attempts`.
5. Lưu thời điểm nộp bài.
6. Trả về kết quả.

**Quy tắc tính điểm đề xuất cho bộ khung:**

- Mỗi câu chỉ có một đáp án đúng.
- Mỗi câu đúng được 1 điểm thô.
- Câu sai hoặc chưa trả lời được 0 điểm.
- Điểm trên thang 10 được tính bằng:

`score = correct_count * 10.0 / total_questions`

- Kết quả hiển thị làm tròn đến 2 chữ số thập phân.
- Việc chấm điểm phải thực hiện tại server, không tin tưởng điểm do client gửi.

Response thành công:

```json
{
  "type": "EXAM_SUBMIT_RESULT",
  "request_id": "req_007",
  "success": true,
  "data": {
    "attempt_id": 10,
    "total_questions": 20,
    "correct_count": 16,
    "score": 8.0,
    "submitted_at": "2026-10-26T13:25:00Z"
  },
  "error": null
}
```

Mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `ATTEMPT_NOT_FOUND` | Lượt thi không hợp lệ |
| `EXAM_ALREADY_SUBMITTED` | Bài thi đã được nộp |
| `DATABASE_ERROR` | Không thể lưu kết quả |

**Lưu ý:** Nếu đã hết giờ, server phải tự kết thúc và chấm lượt thi. Không được phụ thuộc vào việc client gửi request nộp bài.

### 11.5. Xem kết quả — `EXAM_RESULT`

Request:

```json
{
  "type": "EXAM_RESULT",
  "request_id": "req_008",
  "payload": {
    "attempt_id": 10
  }
}
```

Response thành công:

```json
{
  "type": "EXAM_RESULT_RESULT",
  "request_id": "req_008",
  "success": true,
  "data": {
    "attempt_id": 10,
    "room_id": 1,
    "total_questions": 20,
    "correct_count": 16,
    "score": 8.0,
    "started_at": "2026-10-26T13:00:00Z",
    "submitted_at": "2026-10-26T13:25:00Z"
  },
  "error": null
}
```

Quy ước:

- Chỉ trả kết quả của lượt thi đã hoàn thành.
- Người dùng chỉ được xem kết quả của mình.
- Điểm số lấy từ dữ liệu đã chấm tại server.
- Tên response `EXAM_RESULT_RESULT` tuân theo quy tắc chung: tên request cộng `_RESULT`.

Mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `ATTEMPT_NOT_FOUND` | Lượt thi không tồn tại hoặc không thuộc người dùng |
| `EXAM_NOT_SUBMITTED` | Bài thi chưa hoàn thành |
| `DATABASE_ERROR` | Không thể truy vấn kết quả |

### 11.6. Xem lịch sử thi — `EXAM_HISTORY`

Request:

```json
{
  "type": "EXAM_HISTORY",
  "request_id": "req_009",
  "payload": {}
}
```

Response thành công:

```json
{
  "type": "EXAM_HISTORY_RESULT",
  "request_id": "req_009",
  "success": true,
  "data": {
    "attempts": [
      {
        "attempt_id": 10,
        "room_id": 1,
        "room_name": "Phòng thi Lập trình mạng",
        "score": 8.0,
        "started_at": "2026-10-26T13:00:00Z",
        "submitted_at": "2026-10-26T13:25:00Z"
      }
    ]
  },
  "error": null
}
```

Quy ước:

- Chỉ trả về lịch sử của người dùng đang đăng nhập.
- Chỉ bao gồm các lượt thi có `mode = "EXAM"` và đã hoàn thành.
- Sắp xếp lượt thi mới nhất trước.
- Nếu chưa có lịch sử, trả về `"attempts": []`.
- Công Minh sử dụng danh sách này để hiển thị lịch sử và biểu đồ điểm số.

Mã lỗi:

| Mã lỗi | Ý nghĩa |
|---|---|
| `UNAUTHORIZED` | Chưa đăng nhập |
| `DATABASE_ERROR` | Không thể truy vấn lịch sử |

## 12. Các nội dung cần hoàn thiện tiếp

Các API tài khoản, luyện tập và câu hỏi vẫn cần đặc tả chi tiết:

- `AUTH_REGISTER`
- `AUTH_LOGIN`
- `AUTH_LOGOUT`
- `AUTH_PROFILE`
- `AUTH_UPDATE`
- `PRACTICE_START`
- `PRACTICE_SUBMIT`
- `QUESTION_LIST`

Các vấn đề tích hợp cần thống nhất thêm:

- Cơ chế quản lý phiên đăng nhập.
- Quy tắc chuyển trạng thái phòng thi.
- Thời điểm cố định bộ câu hỏi của phòng.
- Cách gateway chuyển tiếp request giữa trình duyệt và TCP Server.
- Cách đồng bộ thời gian thi và tự động nộp bài khi hết giờ.

## 13. Trạng thái tài liệu

Tài liệu đã có đặc tả nền tảng cho giao thức TCP/JSON, quản lý phòng thi và làm bài thi.

Các phần chưa hoàn thiện được liệt kê tại mục 12 và sẽ được bổ sung trước khi tích hợp toàn bộ hệ thống.