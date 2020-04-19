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



// static const char* SERVER_ADDR = "127.0.0.1";
static const char* SERVER_ADDR = "3.121.212.15";
static const unsigned int SERVER_PORT = 12345;

static const int STD_IN = 0;
static const int STD_OUT = 1;

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


void clearline() {
    static const char* buff = "\033[K";
    write(STD_OUT, buff, strlen(buff));
}

void draw(const std::vector<std::string>& rows, const std::string& input, unsigned int maxMessagesCount) {
    write(STD_OUT, "\033[2J", strlen("\033[2J"));
    write(STD_OUT, "\033[0;0H", strlen("\033[0;0H"));
    // write(STD_OUT, "\033[5A", strlen("\033[5A"));
    // static const unsigned int AMOUNT = 4;
    unsigned int curr = (rows.size() > maxMessagesCount) ? (rows.size() - maxMessagesCount) : 0;
    for (unsigned int i = 0; i < maxMessagesCount; i++) {
        if (curr >= rows.size()) clearline();
        else write(STD_OUT, rows[curr].c_str(), rows[curr].size());
        write(STD_OUT, "\n", 1);
        curr++;
    }

    write(STD_OUT, ":>", 2);
    if (input.size() > 0) write(STD_OUT, input.c_str(), input.size());
}

static const unsigned int READ_END = 0;
static const unsigned int WRITE_END = 1;

void asClient(std::string_view nickname) {
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

    // Send out nickname as our first message
    send(socketHandle, nickname.data(), nickname.size(), 0);

    // ========================================================
    // ================ Do stuff ==================
    // ========================================================

    int pipes[2];
    if (pipe(pipes) < 0) {
        std::cerr << "::> Could not create pipes.\n";
        close(socketHandle);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        // Child process.
        // Responsible for network reception

        while (true) {
            if (hasData(socketHandle)) {
                // The server has a new message for us!

                // Receive the message
                const unsigned int MAX_MESSAGE_SIZE = 64;
                char buff[MAX_MESSAGE_SIZE];
                const unsigned int messageSize = recv(socketHandle, buff, MAX_MESSAGE_SIZE, 0);
                // const std::string message(buff, messageSize);

                // Give it to the parent process
                write(pipes[WRITE_END], buff, messageSize);
            }
        }
    } else {
        // Parent process
        // Responsible for graphics.
        // Receives new messages through pipe form the server from the child process.
        // In parent, pid contains the PID of the child

        const unsigned int MAX_MESSAGES_COUNT = 5;
        std::vector<std::string> rows;
        std::string input;

        bool needUpdate = true;
        while (true) {
            // Child process has received a new message from the server?
            if (hasData(pipes[READ_END])) {
                needUpdate = true;
                const unsigned int MAX_MESSAGE_SIZE = 64;
                char buff[MAX_MESSAGE_SIZE];
                const unsigned int count = read(pipes[READ_END], buff, MAX_MESSAGE_SIZE);
                rows.push_back(std::string(buff, count));
            }

            // Input from keyboard?
            if (hasData(STD_IN)) {
                needUpdate = true;
                char c;
                read(STD_IN, &c, 1);
                if (c == '.') {
                    send(socketHandle, "...", 3, 0); // mark for end for servert
                    kill(pid, SIGKILL); // finish shild
                    close(pipes[0]); // TODO: investigate close() return status
                    close(pipes[1]);
                    break; // finish self
                } else if (c == ',') {
                    // Got a new message for the server!
                    send(socketHandle, input.c_str(), input.size(), 0);
                    input.clear();
                } else input += c;
            }

            if (needUpdate) draw(rows, input, MAX_MESSAGES_COUNT);
            needUpdate = false;
            usleep(100000);
        }
    }

    close(socketHandle);

    std::cout << "==================== CLIENT end   ====================\n";
}

struct User {
    int id;
    int socketHandle;
    std::string nickname;

    User(int id, int socketHandle, const std::string& nickname)
        : id(id), socketHandle(socketHandle), nickname(nickname) {}
    User(int id, int socketHandle, std::string&& nickname)
        : id(id), socketHandle(socketHandle), nickname(std::move(nickname)) {}
};

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


    int nextUserId = 0;
    std::vector<User> users;
    while (true) {
        // ========================================================
        // ================ Process new connection ================
        // ========================================================

        if (hasData(listenSocketHandle)) {
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

            // Expect to receive nickname
            const unsigned int MAX_NICKNAME_SIZE = 16;
            char buff[MAX_NICKNAME_SIZE];
            const unsigned int nicknameSize = recv(newSocketHandle, buff, MAX_NICKNAME_SIZE, 0);
            const std::string nickname(buff, nicknameSize);

            // Create a new User
            users.emplace_back(nextUserId++, newSocketHandle, std::move(nickname));
        }

        // ========================================================
        // ================ Check for messages from Users =========
        // ========================================================

        std::vector<int> usersToRemove;
        for (const auto& user : users) {
            // Any new messaged from this user?
            if (!hasData(user.socketHandle)) continue;

            // This user has a new message for us!
            const unsigned int MAX_MESSAGE_SIZE = 64;
            char buff[MAX_MESSAGE_SIZE];
            const unsigned int messageSize = recv(user.socketHandle, buff, MAX_MESSAGE_SIZE, 0);
            const std::string message(buff, messageSize);

            // Request to close connection?
            if (message == "...") {
                std::cout << "User " << user.nickname << " has requested termination\n";
                usersToRemove.push_back(user.id);
                continue;
            }

            // Notify everybody about it!
            // (also send it to the user himself)
            std::string chatMessage(user.nickname);
            chatMessage += ": ";
            chatMessage += message;
            // TODO: should not send to the users that have exited
            for (const auto& targetUser : users) {
                send(targetUser.socketHandle, chatMessage.c_str(), chatMessage.size(), 0);
            }
        }

        // Remove the users that have requested session finish
        for (int id : usersToRemove) {
            const auto it = std::find_if(users.begin(), users.end(),
                    [id](const User& user){ return user.id == id; });
            users.erase(it);
        }

        // ========================================================
        // ================ misc ==================================
        // ========================================================

        if (hasData(STD_IN)) {
            char c;
            read(0, &c, 1);
            if (c == '.') break;
        }

        usleep(100000);
    }

    for (const auto& user : users) close(user.socketHandle);
    close(listenSocketHandle);

    std::cout << "==================== SERVER end   ====================\n";
}



int main(int argc, char* argv[]) {
    termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    if (startAsServer(argc, argv)) asServer();
    else asClient(argv[2]);

    return 0;
}
