# RAT (Remote Administration Tool)

Một công cụ quản trị từ xa đơn giản được viết bằng C++, cho phép điều khiển máy tính từ xa qua mạng.

## Tính năng

- **Quản lý file**:
  - Liệt kê file/thư mục (`LIST`)
  - Tải file từ client về server (`GETFILE`)
  - Tải cả thư mục (`GETDIR`)
  - Sửa nội dung file (`EDITFILE`)
  - Tạo file mới (`CREATEFILE`)
  - Xóa file (`REMOVEFILE`)
  - Chạy file thực thi (`RUNFILE`)

- **Quản lý tiến trình**:
  - Xem danh sách tiến trình (`PS`)
  - Kết thúc tiến trình (`KILL PID`)

- **Thông tin hệ thống**:
  - Lấy thông tin cấu hình (`SYSINFO`)

## Yêu cầu hệ thống

### Server (Windows)
- Windows 7/8/10/11
- Trình biên dịch hỗ trợ C++11
- Thư viện `winsock2`

### Client (Linux)
- Hệ điều hành Linux
- Trình biên dịch hỗ trợ C++17
- Thư viện `filesystem` tiêu chuẩn

## Cài đặt

### Server (Windows)
```bash
g++ server.cpp -o server.exe -lws2_32
```

### Client (Linux)
```bash
g++ -std=c++17 client.cpp -o client
```

## Sử dụng

1. **Cấu hình IP**: Sửa địa chỉ IP trong `client.cpp` thành IP của server
   ```cpp
   inet_pton(AF_INET, "192.168.0.2", &serverAddr.sin_addr)
   ```

2. **Khởi chạy Server**:
   ```bash
   server.exe
   ```
   Server sẽ lắng nghe trên cổng 8080

3. **Khởi chạy Client**:
   ```bash
   ./client
   ```

4. **Các lệnh hỗ trợ**:
   ```
   LIST           - Liệt kê file trong thư mục hiện tại
   GETFILE <tên>  - Tải file về server
   GETDIR <tên>   - Tải thư mục về server
   PS             - Xem danh sách tiến trình
   KILL <PID>     - Kết thúc tiến trình
   EDITFILE <tên> - Sửa nội dung file
   CREATEFILE <tên> - Tạo file mới
   REMOVEFILE <tên> - Xóa file
   RUNFILE <tên>  - Chạy file thực thi
   SYSINFO        - Lấy thông tin hệ thống
   EXIT           - Thoát
   ```

## Kiến trúc

- **Server**: Chạy trên Windows, nhận lệnh từ người dùng và gửi đến Client
- **Client**: Chạy trên Linux, thực thi lệnh và gửi kết quả về Server
- **Giao tiếp**: Socket TCP/IP qua cổng 8080

## Lưu ý

- File tải từ client được lưu trong thư mục `downloads/`
- Khi tải thư mục, client sẽ nén thành `.tar.gz` và server tự động giải nén
- Cần có quyền thực thi (chmod +x) cho file để chạy lệnh `RUNFILE`

## Cảnh báo

Phần mềm này chỉ dành cho mục đích học tập và quản trị hệ thống hợp pháp. Vui lòng không sử dụng cho mục đích trái phép.

## License

MIT License - Sử dụng cho mục đích học tập và nghiên cứu.