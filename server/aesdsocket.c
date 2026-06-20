#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;

void cleanup() {
    if (client_fd != -1) {
        close(client_fd);
    }
    if (server_fd != -1) {
        close(server_fd);
    }
    remove(DATA_FILE);
    closelog();
}

static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        cleanup();
        exit(EXIT_SUCCESS);
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (setsid() < 0) exit(EXIT_FAILURE);

    pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);

    if (chdir("/") < 0) exit(EXIT_FAILURE);

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);
}

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = true;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        syslog(LOG_ERR, "Socket creation failed");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Setsockopt failed");
        cleanup();
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        syslog(LOG_ERR, "Bind failed");
        cleanup();
        return -1;
    }

    if (daemon_mode) {
        daemonize();
    }

    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listen failed");
        cleanup();
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            syslog(LOG_ERR, "Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        FILE *data_file = fopen(DATA_FILE, "a+");
        if (!data_file) {
            syslog(LOG_ERR, "Failed to open data file");
            close(client_fd);
            continue;
        }

        char read_buf[BUFFER_SIZE];
        ssize_t bytes_received;
        bool newline_found = false;

        while ((bytes_received = recv(client_fd, read_buf, BUFFER_SIZE, 0)) > 0) {
            fwrite(read_buf, 1, bytes_received, data_file);
            
            for (ssize_t i = 0; i < bytes_received; i++) {
                if (read_buf[i] == '\n') {
                    newline_found = true;
                    break;
                }
            }
            if (newline_found) {
                break;
            }
        }

        fflush(data_file);
        fseek(data_file, 0, SEEK_SET);

        size_t bytes_read;
        while ((bytes_read = fread(read_buf, 1, BUFFER_SIZE, data_file)) > 0) {
            send(client_fd, read_buf, bytes_read, 0);
        }

        fclose(data_file);
        close(client_fd);
        client_fd = -1;
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    cleanup();
    return 0;
}
