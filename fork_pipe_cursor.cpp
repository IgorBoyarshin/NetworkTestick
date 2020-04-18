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


static const int STD_IN = 0;
static const int STD_OUT = 1;

void clearline() {
    static const char* buff = "\033[K";
    write(STD_OUT, buff, strlen(buff));
}

void draw(const std::vector<std::string>& rows, const std::string& input) {
    write(STD_OUT, "\033[2J", strlen("\033[2J"));
    write(STD_OUT, "\033[0;0H", strlen("\033[0;0H"));
    // write(STD_OUT, "\033[5A", strlen("\033[5A"));
    static const unsigned int AMOUNT = 4;
    unsigned int curr = (rows.size() > AMOUNT) ? (rows.size() - AMOUNT) : 0;
    for (unsigned int i = 0; i < AMOUNT; i++) {
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

int main(int argc, char* argv[]) {
    termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    tty.c_lflag &= ~ECHO;
    tty.c_lflag &= ~ICANON;
    tcsetattr(STDIN_FILENO, TCSANOW, &tty);

    int pipes[2];
    if (pipe(pipes) < 0) exit(-1);

    pid_t pid = fork();
    if (pid == 0) {
        // Imitates data received over network
        for (int i = 0;; i++) {
            sleep(1);
            std::string content("MSG number ");
            content += std::to_string(i);
            write(pipes[WRITE_END], content.c_str(), content.size());
        }
    } else { // parent
        // In parent, pid contains the PID of the child

        std::vector<std::string> rows;
        std::string input;

        bool needUpdate;
        while (true) {
            needUpdate = false;

            if (hasData(pipes[READ_END])) {
                needUpdate = true;
                std::cout << "Got data" << '\n';
                char buff[32];
                const unsigned int count = read(pipes[READ_END], buff, 32);
                rows.push_back(std::string(buff, count));
            }

            if (hasData(0)) {
                needUpdate = true;
                char c;
                read(0, &c, 1);
                input += c;
                if (c == '.') {
                    kill(pid, SIGKILL); // finish shild
                    close(pipes[0]); // TODO: investigate close() return status
                    close(pipes[1]);
                    break; // finish self
                }
                if (c == ',') {
                    rows.push_back(std::string("From me:") + input + ";");
                    input.clear();
                }
            }

            if (needUpdate) draw(rows, input);
            usleep(100);
        }
    }

    return 0;
}
