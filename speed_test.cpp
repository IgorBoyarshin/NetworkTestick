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
#include <algorithm>



// const char* SERVER_ADDR = "127.0.0.1";
// const char* SERVER_ADDR = "0.0.0.0";
const char* SERVER_ADDR = "3.121.212.15";
const unsigned int SERVER_PORT = 12345;

const int STD_IN = 0;
const int STD_OUT = 1;

const unsigned int READ_END = 0;
const unsigned int WRITE_END = 1;

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


void asClient() {
    std::cout << "==================== CLIENT start ====================\n";

    // ========================================================
    // ================ Establish connection ==================
    // ========================================================

    const int socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socketHandle < 0) {
        std::cerr << "::> Could not create socket.\n";
        return;
    }

    // Initialize struct containing destination (server) info
    sockaddr_in serverSocketInfo;
    bzero(&serverSocketInfo, sizeof(sockaddr_in));
    serverSocketInfo.sin_family = AF_INET;
    serverSocketInfo.sin_port = htons(SERVER_PORT);
    inet_aton(SERVER_ADDR, &serverSocketInfo.sin_addr);
    if (connect(socketHandle,
                reinterpret_cast<sockaddr*>(&serverSocketInfo),
                sizeof(serverSocketInfo)) < 0) {
        std::cerr << "::> Could not connect()\n";
        close(socketHandle);
        return;
    }

    std::cout << ":> Established connection...\n";

    // ========================================================
    // ================ Do stuff ==============================
    // ========================================================

    const unsigned int DATA_SIZE = 64;
    char data[DATA_SIZE];
    send(socketHandle, data, DATA_SIZE, 0);
    recv(socketHandle, data, DATA_SIZE, 0);

    close(socketHandle);

    std::cout << "==================== CLIENT end   ====================\n";
}

void asServer() {
    std::cout << "==================== SERVER start ====================\n";

    // ========================================================
    // ================ Setup listen socket ===================
    // ========================================================

    static const auto handleSignal = [](int signum){ while (waitpid(-1, NULL, WNOHANG) > 0); };
    signal(SIGCHLD, handleSignal); // TODO: check if needed

    const int listenSocketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocketHandle < 0) {
        std::cerr << "::> Failed to create socket.\n";
        return;
    }

    // To release the resource (port) immediately after closing it.
    // Prevents bind error when trying to bind to the same port having closed it.
    int option = 1; // TODO: confirm that the variable may be disposed of
    setsockopt(listenSocketHandle, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));

    // Initialize struct containing self (server) info
    sockaddr_in selfSocketInfo;
    bzero(&selfSocketInfo, sizeof(sockaddr_in));
    selfSocketInfo.sin_family = AF_INET;
    selfSocketInfo.sin_port = htons(SERVER_PORT);
    inet_aton(SERVER_ADDR, &selfSocketInfo.sin_addr);
    if (bind(listenSocketHandle, reinterpret_cast<sockaddr*>(&selfSocketInfo), sizeof(selfSocketInfo)) < 0) {
        std::cerr << "::> Unable to bind: " << strerror(errno) << '\n';
        close(listenSocketHandle);
        return;
    }

    // Currently, the client is stalled if the queue is overflowed, so try
    // to keep MAX_BUFF_SIZE relatively big
    const unsigned int MAX_IN_QUEUE = 16;
    listen(listenSocketHandle, MAX_IN_QUEUE);

    std::cout << ":> Server now listening for connections...\n";

    // Initialize struct containing incoming (client) info
    sockaddr_in incomingSocketInfo;
    socklen_t incomingSocketSize = sizeof(incomingSocketInfo);
    const int newSocketHandle = accept(
            listenSocketHandle,
            reinterpret_cast<sockaddr*>(&incomingSocketInfo),
            &incomingSocketSize);
    if (newSocketHandle < 0) {
        std::cerr << "::> Unable to accept connection.\n";
        close(listenSocketHandle);
        return;
    }

    const unsigned int DATA_SIZE = 64;
    char data[DATA_SIZE];
    recv(newSocketHandle, data, DATA_SIZE, 0);
    send(newSocketHandle, data, DATA_SIZE, 0);

    close(newSocketHandle);
    close(listenSocketHandle);

    std::cout << "==================== SERVER end   ====================\n";
}



int main(int argc, char* argv[]) {
    if (startAsServer(argc, argv)) asServer();
    else asClient();

    return 0;
}
