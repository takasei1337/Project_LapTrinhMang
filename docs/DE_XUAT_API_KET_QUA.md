# Đề xuất API — Xem lại bài thi và kết quả phòng thi (Công Minh)

**Phạm vi:** mục 17 "Xem kết quả thi của các phòng thi" và mục 18 "Lưu trữ thông tin bài thi + đồ thị".
**Trạng thái:** Chờ nhóm thống nhất định dạng JSON trước khi đưa vào `docs/protocol.md` và `server/include/protocol.h`.

Các tính năng đã được phân công từ đầu. Tài liệu này chỉ bổ sung **định dạng JSON** còn thiếu trong `protocol.md`.

## 1. Quyền xem

Giáo viên là tài khoản `ADMIN` theo cơ chế phân quyền RBAC (tính năng 3 của Thế Anh) hoặc chủ phòng. Server lấy vai trò từ `users.role`, không tin dữ liệu client gửi lên.

| Người xem | `EXAM_REVIEW` (xem lại bài) | `ROOM_RESULT` (kết quả phòng) |
|---|---|---|
| Giáo viên (chủ phòng hoặc ADMIN) | Bài của mọi thí sinh trong phòng, bất kỳ lúc nào | Được xem |
| Thí sinh | Chỉ bài của mình, **chỉ khi phòng đã kết thúc** | `FORBIDDEN` |
| Người khác | `ATTEMPT_NOT_FOUND` | `FORBIDDEN` |

Thí sinh chỉ xem được đáp án khi phòng đã kết thúc (`status = FINISHED` hoặc đã qua `end_time`). Lý do: cả phòng dùng chung bộ đề, nên nếu bạn nộp sớm thấy đáp án ngay thì có thể gửi cho bạn đang thi.

Mã lỗi mới: `ROOM_NOT_FINISHED` — Chỉ xem được khi phòng thi đã kết thúc.

## 2. `EXAM_REVIEW` — Xem lại bài đã nộp

Request:

```json
{ "type": "EXAM_REVIEW", "request_id": "req_010", "payload": { "attempt_id": 10 } }
```

Response thành công:

```json
{
  "type": "EXAM_REVIEW_RESULT",
  "request_id": "req_010",
  "success": true,
  "data": {
    "attempt_id": 10,
    "room_id": 1,
    "room_name": "Phòng thi Lập trình mạng",
    "user_id": 1,
    "username": "minh",
    "total_questions": 20,
    "correct_count": 16,
    "score": 8.0,
    "started_at": "2026-10-26T13:00:00Z",
    "submitted_at": "2026-10-26T13:25:00Z",
    "questions": [
      {
        "position": 1,
        "question_id": 101,
        "content": "Giao thức nào hoạt động ở tầng vận chuyển?",
        "topic": "Mạng máy tính",
        "difficulty": "EASY",
        "selected_answer_id": 1002,
        "correct_answer_id": 1001,
        "result": "WRONG",
        "answers": [
          { "answer_id": 1001, "content": "TCP", "is_correct": true },
          { "answer_id": 1002, "content": "IP", "is_correct": false }
        ]
      }
    ]
  },
  "error": null
}
```

- `result` nhận một trong ba giá trị `CORRECT`, `WRONG`, `UNANSWERED`.
- Câu bỏ trống có `selected_answer_id` bằng `null`.
- Mã lỗi: `UNAUTHORIZED`, `ATTEMPT_NOT_FOUND`, `EXAM_NOT_SUBMITTED`, `ROOM_NOT_FINISHED`, `DATABASE_ERROR`.
- Hàm C: `char *exam_review(sqlite3 *db, int attempt_id, int user_id);`

## 3. `ROOM_RESULT` — Kết quả thi của phòng (dành cho giáo viên)

Request:

```json
{ "type": "ROOM_RESULT", "request_id": "req_011", "payload": { "room_id": 1 } }
```

Response thành công:

```json
{
  "type": "ROOM_RESULT_RESULT",
  "request_id": "req_011",
  "success": true,
  "data": {
    "room_id": 1,
    "room_name": "Phòng thi Lập trình mạng",
    "owner_id": 2,
    "status": "FINISHED",
    "question_count": 20,
    "duration_minutes": 30,
    "member_count": 30,
    "submitted_count": 27,
    "in_progress_count": 0,
    "not_started_count": 3,
    "average_score": 7.25,
    "highest_score": 10.0,
    "lowest_score": 3.5,
    "results": [
      {
        "user_id": 3,
        "username": "an",
        "attempt_id": 41,
        "score": 8.75,
        "correct_count": 7,
        "total_questions": 8,
        "started_at": "2026-10-26T13:00:00Z",
        "submitted_at": "2026-10-26T13:06:00Z",
        "duration_seconds": 360
      }
    ],
    "not_started": [ { "user_id": 7, "username": "em" } ]
  },
  "error": null
}
```

- `member_count` không tính chủ phòng.
- Mỗi thí sinh có một dòng trong `results`, lấy lượt thi có điểm cao nhất. Danh sách sắp xếp theo tên.
- Không xếp hạng ở đây. Bảng xếp hạng là phần của Tuấn Anh (mục 12) và có thể dùng lại dữ liệu này.
- `average_score`, `highest_score`, `lowest_score` bằng `null` khi chưa ai nộp bài.
- Mã lỗi: `UNAUTHORIZED`, `ROOM_NOT_FOUND`, `FORBIDDEN`, `DATABASE_ERROR`.
- Hàm C: `char *exam_room_result(sqlite3 *db, int room_id, int user_id);`

## 4. Cần thêm vào `server/include/protocol.h` sau khi thống nhất

```c
#define REQ_EXAM_REVIEW       "EXAM_REVIEW"
#define REQ_ROOM_RESULT       "ROOM_RESULT"
```

## 5. Giao diện cần các bạn cung cấp

Phần này thuộc phạm vi của các bạn khác. Cách làm cụ thể do các bạn quyết định; giao diện chỉ cần biết tên request và JSON để hiển thị. Hiện mock server đang giả lập tạm các phần này.

**Tuấn Anh (Room):**

- Thiết lập bộ đề cho phòng (lưu vào `room_questions`). Module Exam chỉ đọc bộ đề có sẵn; phòng chưa có đề thì trả `EXAM_NOT_AVAILABLE`.
- Cách giáo viên mở phòng (`WAITING → IN_PROGRESS`) và kết thúc phòng (`IN_PROGRESS → FINISHED`). Sau khi kết thúc phòng, gọi `exam_finalize_expired(db)` để thu và chấm các bài đang làm.
- Chỉ ADMIN được tạo phòng. Giao diện đã ẩn nút "Tạo phòng" với học sinh.
- Số thí sinh và trạng thái đã tham gia của người dùng trong `ROOM_LIST`.
- Cách báo cho học sinh khi có phòng mới. Trong lúc chờ, giao diện tự tải lại danh sách phòng mỗi 5 giây.

**Thế Anh (Auth):**

- Payload của `AUTH_REGISTER`. Mock đang tạm dùng `username` và `password`.
