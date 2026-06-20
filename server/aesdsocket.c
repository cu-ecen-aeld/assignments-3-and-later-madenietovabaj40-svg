#include <sys/stat.h>
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

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

static int server_fd = -1;
static int client_fd = -1;
static volatile sig_atomic_t keep_running = 1;

static void signal_handler(int sig)
{
    (void)sig;
    keep_running = 0;

    // Принудительно будим блокирующий accept()
    if (server_fd != -1) {
        shutdown(server_fd, SHUT_RDWR);
    }
}
static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) exit(-1);
    if (pid > 0) exit(0);

    setsid();

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

int main(int argc, char *argv[])
{
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
    if (server_fd < 0) {
        syslog(LOG_ERR, "socket failed: %m");
        return -1;
    }

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
    socklen_t client_len;

    while (keep_running) {
    // ВАЖНО: сбрасываем размер перед КАЖДЫМ вызовом accept
    client_len = sizeof(client_addr); 
    
    client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
    
    if (client_fd == -1) {
        // Если сокет закрыли через shutdown во время сигнала, accept вернет -1.
        // Если мы закрываемся, то просто выходим из цикла без паники
        if (!keep_running) {
            break; 
        }
        perror("accept failed");
        continue;
    }
    
    // Дальше твой код обработки чтения/записи...
}
        // Логирование IP при подключении (Строгое требование AESD)
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        char *recv_buf = malloc(BUFFER_SIZE);
        if (!recv_buf)
            break;

        size_t buf_size = BUFFER_SIZE;
        size_t data_len = 0;

        while (keep_running) {

            char chunk[BUFFER_SIZE];
            ssize_t bytes = recv(client_fd, chunk, sizeof(chunk), 0);

            if (bytes <= 0)
                break;

            if (data_len + bytes >= buf_size) {
                buf_size += bytes + BUFFER_SIZE;
                char *tmp = realloc(recv_buf, buf_size);
                if (!tmp)
                    break;
                recv_buf = tmp;
            }

            memcpy(recv_buf + data_len, chunk, bytes);
            data_len += bytes;

            char *newline;

            while ((newline = memchr(recv_buf, '\n', data_len)) != NULL) {

                size_t len = (newline - recv_buf) + 1;

                // Запись с проверкой на успешное открытие
                FILE *f = fopen(DATA_FILE, "a");
                if (f) {
                    fwrite(recv_buf, 1, len, f);
                    fclose(f);
                }

                // Чтение с проверкой на успешное открытие
                FILE *rf = fopen(DATA_FILE, "r");
                if (rf) {
                    char send_buf[BUFFER_SIZE];
                    size_t n;
                    while ((n = fread(send_buf, 1, sizeof(send_buf), rf)) > 0) {
                        send(client_fd, send_buf, n, 0);
                    }
                    fclose(rf);
                }

                memmove(recv_buf, recv_buf + len, data_len - len);
                data_len -= len;
            }
        }

        free(recv_buf);
        close(client_fd);
        client_fd = -1;

        // Логирование закрытия (Строгое требование AESD)
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    if (server_fd != -1)
        close(server_fd);

    unlink(DATA_FILE);
    syslog(LOG_INFO, "Server shutdown complete");
    closelog();

    return 0;
}
