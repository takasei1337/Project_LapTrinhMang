
# Dự án Lập trình mạng — Ứng dụng thi trắc nghiệm trực tuyến

## 1. Giới thiệu

Ứng dụng thi trắc nghiệm trực tuyến được xây dựng bằng ngôn ngữ C, sử dụng TCP Socket và SQLite3.

Các chức năng chính:
- Đăng ký, đăng nhập và quản lý tài khoản.
- Tạo phòng thi, xem danh sách phòng và tham gia phòng.
- Bắt đầu thi, chọn/sửa đáp án, nộp bài và chấm điểm.
- Luyện tập trắc nghiệm.
- Phân loại câu hỏi theo chủ đề và độ khó.
- Xem kết quả, lịch sử thi và biểu đồ.
- Ghi nhật ký hoạt động.

## 2. Công nghệ

- Backend: C11, POSIX TCP Socket.
- Xử lý kết nối: `poll()`.
- Giao thức: JSON (cJSON), có tiền tố độ dài 4 byte.
- Cơ sở dữ liệu: SQLite3.
- Giao diện: HTML, CSS, JavaScript.
- Kết nối giao diện web: HTTP/WebSocket gateway (sẽ triển khai).
- Môi trường phát triển: Ubuntu WSL2, GCC, Makefile.

## 3. Phân công công việc

### Thế Anh

- Đăng ký, đăng nhập và quản lý phiên đăng nhập.
- Phân quyền truy cập.
- Chế độ luyện tập.
- Phân loại câu hỏi.
- Ghi nhật ký hoạt động.

Thư mục phụ trách:
- `server/src/auth/`
- `server/src/practice/`
- `server/src/question/`
- `server/src/logger/`

### Tuấn Anh

- Xây dựng TCP Socket Server.
- Xử lý luồng dữ liệu TCP và nhiều client.
- Tạo phòng thi, danh sách phòng và tham gia phòng.
- Định tuyến request đến các module.
- Xây dựng gateway kết nối giao diện web với TCP Server.

Thư mục phụ trách:
- `server/src/network/`
- `server/src/room/`
- `server/src/main.c`

### Công Minh

- Xây dựng giao diện web.
- Bắt đầu thi, sửa đáp án, nộp bài và chấm điểm.
- Hiển thị kết quả phòng thi, lịch sử thi và biểu đồ.

Thư mục phụ trách:
- `client/`
- `server/src/exam/`

## 4. Cấu trúc dự án

- `server/include/`: Các header API dùng chung.
- `server/src/`: Các module backend.
- `client/`: Giao diện web.
- `database/`: Cấu trúc cơ sở dữ liệu.
- `docs/`: Tài liệu giao thức và hướng dẫn.
- `tests/`: Chương trình kiểm thử.
- `Makefile`: Biên dịch dự án.

## 5. Quy ước chung

1. Không tự ý đổi tên request trong `protocol.h`.
2. Đọc `docs/protocol.md` trước khi viết code liên quan đến mạng.
3. Không sửa module của thành viên khác khi chưa thống nhất.
4. Không viết trực tiếp logic nghiệp vụ vào `main.c`.
5. Các module nghiệp vụ không gọi trực tiếp `send()` hoặc `recv()`.
6. Không commit file thực thi, cơ sở dữ liệu runtime hoặc thông tin bí mật.
7. Mỗi thành viên làm trên branch riêng và tạo pull request.

## 6. Cài đặt và chạy

### Yêu cầu môi trường

- Ubuntu hoặc Ubuntu trên WSL2.
- GCC và Make.
- SQLite3 và thư viện phát triển SQLite3.

### Cài đặt công cụ

Mở Terminal Ubuntu và chạy:

```bash
sudo apt update
sudo apt install -y build-essential make sqlite3 libsqlite3-dev libcjson-dev
```

### Biên dịch dự án

Tại thư mục gốc của repository:

```bash
make clean && make
```

Nếu thành công, chương trình thực thi sẽ được tạo tại:

`server/server`


### Khởi tạo cơ sở dữ liệu

Sau khi cài đặt các thư viện và biên dịch dự án, chạy:

```bash
make init-db
```

Lệnh này tạo file `database/app.db` và các bảng theo `database/schema.sql`.

**Lưu ý:**

- Chỉ chạy `make init-db` khi chưa có database.
- Không chạy lại trên database đã có bảng vì có thể phát sinh lỗi `table already exists`.
- File `database/app.db` được Git bỏ qua và không đưa lên GitHub.
- Mỗi thành viên cần tự khởi tạo database trên máy của mình sau khi clone dự án.

### Chạy server

```bash
make run
```

**Lưu ý:** Server hiện mới có phần khởi tạo socket và `bind()`, chưa triển khai `listen()`, `accept()` hoặc xử lý request. Tuấn Anh sẽ hoàn thiện các chức năng này.

### Kiểm tra cấu trúc database

```bash
sqlite3 :memory: < database/schema.sql && echo "Schema OK"
```

Nếu kết quả hiển thị `Schema OK`, file schema không có lỗi khi thực thi.

### Dọn file biên dịch

```bash
make clean
```


## 7. Hướng dẫn bắt đầu cho từng thành viên

Sau khi clone repository, mỗi thành viên đọc tài liệu tương ứng trước khi viết code.

### Tuấn Anh — Network và Room

**[Đọc hướng dẫn dành cho Tuấn Anh](docs/HUONG_DAN_TUAN_ANH.md)**

Phụ trách TCP Server, xử lý nhiều client, quản lý phòng thi và tích hợp kết nối mạng.

### Công Minh — GUI và Exam

**[Đọc hướng dẫn dành cho Công Minh](docs/HUONG_DAN_CONG_MINH.md)**

Phụ trách giao diện, xử lý bài thi, chấm điểm, kết quả và lịch sử thi.

### Thế Anh — Auth, Practice, Question và Logger

Phụ trách tài khoản, phiên đăng nhập, phân quyền, luyện tập, phân loại câu hỏi và nhật ký hoạt động.

### Tài liệu kỹ thuật chung

- [Giao thức truyền thông](docs/protocol.md)
- [Cấu trúc cơ sở dữ liệu](database/schema.sql)

## 8. Quy trình làm việc nhóm

1. Clone repository từ GitHub.
2. Đọc `README.md` và tài liệu được phân công.
3. Tạo branch riêng cho công việc của mình.
4. Chỉ chỉnh sửa các module thuộc phạm vi phụ trách.
5. Chạy `make clean && make` để kiểm tra trước khi đẩy code.
6. Tạo pull request để tích hợp vào branch chính.

## 9. Trạng thái dự án

Dự án đang trong giai đoạn xây dựng bộ khung chung.

Các chức năng nghiệp vụ chưa được triển khai đầy đủ.
```

