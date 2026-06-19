#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;
volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        keep_running = 0;
        if (client_fd != -1) close(client_fd);
        if (server_fd != -1) close(server_fd);
    }
}

void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) exit(-1);
    if (pid > 0) exit(0);
    if (setsid() < 0) exit(-1);
    pid = fork();
    if (pid < 0) exit(-1);
    if (pid > 0) exit(0);
    chdir("/");
    umask(0);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemonize();
    }
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { syslog(LOG_ERR, "socket failed: %m"); return -1; }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        syslog(LOG_ERR, "bind failed: %m");
        close(server_fd);
        return -1;
    }
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "listen failed: %m");
        close(server_fd);
        return -1;
    }
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (keep_running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) { if (errno == EINTR) break; continue; }
        char *recv_buf = malloc(BUFFER_SIZE);
        size_t buf_size = BUFFER_SIZE;
        size_t data_len = 0;
        while (keep_running) {
            char chunk[BUFFER_SIZE];
            ssize_t bytes = recv(client_fd, chunk, sizeof(chunk), 0);
            if (bytes <= 0) break;
            if (data_len + bytes >= buf_size) {
                buf_size += bytes + BUFFER_SIZE;
                char *tmp = realloc(recv_buf, buf_size);
                if (!tmp) { free(recv_buf); recv_buf = NULL; break; }
                recv_buf = tmp;
            }
            memcpy(recv_buf + data_len, chunk, bytes);
            data_len += bytes;
            char *newline;
            while ((newline = memchr(recv_buf, '\n', data_len)) != NULL) {
                size_t line_len = newline - recv_buf + 1;
                FILE *f = fopen(DATA_FILE, "a");
                if (f) { fwrite(recv_buf, 1, line_len, f); fclose(f); }
                FILE *rf = fopen(DATA_FILE, "r");
                if (rf) {
                    char send_buf[BUFFER_SIZE];
                    size_t r;
                    while ((r = fread(send_buf, 1, sizeof(send_buf), rf)) > 0)
                        send(client_fd, send_buf, r, 0);
                    fclose(rf);
                }
                memmove(recv_buf, recv_buf + line_len, data_len - line_len);
                data_len -= line_len;
            }
        }
        free(recv_buf);
        close(client_fd);
        client_fd = -1;
    }
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    closelog();
    return 0;
}
