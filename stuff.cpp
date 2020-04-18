#include <cstring>
#include <iostream>



// =============
// TODO: check if needed
// =============
// Network-related
#include <unistd.h> // close socket
#include <arpa/inet.h> // inet_aton()
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <errno.h>
// System-related
#include <signal.h>
#include <sys/wait.h>

// #include <ncurses.h>
#include <termios.h>
#include <optional>
#include <vector>



static const unsigned int SERVER_PORT = 1234;
static const char* SERVER_ADDR = "127.0.0.1";

bool startAsServer(int argc, char* argv[]) {
    if (argc <= 1) return true;
    if (strcmp(argv[1], "server") == 0) return true;
    if (strcmp(argv[1], "client") == 0) return false;
    std::cerr << "::> Unknown first argument. Will start as Server\n";
    return true;
}

bool hasData(int socketHandle) noexcept {
    fd_set checkReadSet;
    timeval timeout = { 0, 0 }; // results in immediate return (used for polling) if no data ready

    FD_ZERO(&checkReadSet);
    FD_SET(socketHandle, &checkReadSet);

    const int readyDescriptorsAmount = select(socketHandle + 1, &checkReadSet,
            static_cast<fd_set*>(0), static_cast<fd_set*>(0), &timeout);

    if (readyDescriptorsAmount > 0) return FD_ISSET(socketHandle, &checkReadSet);
    if (readyDescriptorsAmount == -1) { // error
        std::cerr << "::> select() returned -1: " << strerror(errno) << '\n';
    } // otherwise == 0 => no fitting descriptors found => no data ready
    return false;
}

void server() {
    std::cout << "==================== SERVER start ====================\n";
    static const char* m_Address = "127.0.0.1";
    static const unsigned int m_Port = 1234;

    static const auto handleSignal = [](int signum){ while (waitpid(-1, NULL, WNOHANG) > 0); };
    signal(SIGCHLD, handleSignal); // TODO: check if needed

    const int m_SocketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_SocketHandle < 0) {
        std::cerr << "::> Failed to create socket.\n";
        return;
    }

    // To release the resource (port) immediately after closing it.
    // Prevents bind error when trying to bind to the same port having closed it.
    int option = 1; // TODO: confirm that the variable may be disposed of
    setsockopt(m_SocketHandle, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // Initialize struct containing self (server) info
    sockaddr_in selfSocketInfo;
    bzero(&selfSocketInfo, sizeof(sockaddr_in));
    selfSocketInfo.sin_family = AF_INET;
    selfSocketInfo.sin_port = htons(m_Port);
    inet_aton(m_Address, &selfSocketInfo.sin_addr);

    if (bind(m_SocketHandle, reinterpret_cast<sockaddr*>(&selfSocketInfo), sizeof(selfSocketInfo)) < 0) {
        std::cerr << "::> Unable to find: " << strerror(errno) << '\n';
        close(m_SocketHandle);
        return;
    }

    // Currently, the client is stalled if the queue is overflowed, so try
    // to keep MAX_BUFF_SIZE relatively big
    const unsigned int MAX_IN_QUEUE = 16;
    listen(m_SocketHandle, MAX_IN_QUEUE);
    std::cout << "Now listening for connections...\n";

    while (!hasData(m_SocketHandle)) {
        std::cout << "no data..." << '\n';
        sleep(1);
    }

    // Initialize struct containing incoming (client) info
    sockaddr_in incomingSocketInfo;
    socklen_t incomingSocketSize = sizeof(incomingSocketInfo);
    const int socketConnection = accept(
            m_SocketHandle,
            reinterpret_cast<sockaddr*>(&incomingSocketInfo),
            &incomingSocketSize);
    if (socketConnection < 0) {
        std::cerr << "::> Unable to accept connection.\n";
        close(m_SocketHandle);
        return;
    }

    static const unsigned int MAX_BUFF_SIZE = 1024;
    char buff[MAX_BUFF_SIZE];
    const int receivedBytesCount = recv(socketConnection, buff, MAX_BUFF_SIZE, 0);
    // memset(buff + receivedBytesCount, 0,
    //     (MAX_BUFF_SIZE - receivedBytesCount) * sizeof(char)); // zero the remaining chunk

    const std::string received(buff, receivedBytesCount);

    std::cout << ":> Received packet from [" << inet_ntoa(incomingSocketInfo.sin_addr)
        << ":" << ntohs(incomingSocketInfo.sin_port)
        << "]=" << received << ";\n";

    const char* answer = "acknowledged\n";
    send(socketConnection, answer, strlen(answer), 0);

    close(socketConnection);
    close(m_SocketHandle);

    std::cout << "==================== SERVER end   ====================\n";
}

void client() {
    std::cout << "==================== CLIENT start ====================\n";
    static const unsigned int m_ServerPort = 1234;
    static const char* m_ServerAddress = "127.0.0.1";

    const int socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle < 0) {
        std::cerr << "::> Could not create socket.\n";
        return;
    }

    // Initialize struct containing destination (server) info
    sockaddr_in serverSocketInfo;
    bzero(&serverSocketInfo, sizeof(sockaddr_in));
    serverSocketInfo.sin_family = AF_INET;
    serverSocketInfo.sin_port = htons(m_ServerPort);
    inet_aton(m_ServerAddress, &serverSocketInfo.sin_addr);

    if (connect(socketHandle,
                reinterpret_cast<sockaddr*>(&serverSocketInfo),
                sizeof(serverSocketInfo)) < 0) {
        std::cerr << "::> Could not connect()\n";
        close(socketHandle);
        return;
    }

    std::cout << ":> Have connection.\n";

    static const char* message = "Hello from client\n";
    send(socketHandle, message, strlen(message), 0);

    static const unsigned int MAX_BUFF_SIZE = 1024;
    char buff[MAX_BUFF_SIZE];
    const int receivedBytesCount = recv(socketHandle, buff, MAX_BUFF_SIZE, 0);
    const std::string received(buff, receivedBytesCount);
    std::cout << "Got: " << received << '\n';

    close(socketHandle);

    std::cout << "==================== CLIENT end   ====================\n";
}
