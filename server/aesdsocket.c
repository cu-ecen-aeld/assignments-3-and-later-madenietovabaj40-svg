#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

// Глобальные дескрипторы для безопасной очистки при сигналах
int server_fd = -1;
int client_fd = -1;
volatile sig_atomic_t keep_running = 1;

// Обработчик сигналов завершения
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        keep_running = 0;
        // Завершаем заблокированные системные вызовы сокетов
        if (client_fd != -1) {
            shutdown(client_fd, SHUT_RDWR);
            close(client_fd);
            client_fd = -1;
        }
        if (server_fd != -1) {
            shutdown(server_fd, SHUT_RDWR);
            close(server_fd);
            server_fd = -1;
        }
    }
}

// Функция перевода процесса в режим демона
void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(-1);
    if (pid > 0) exit(0); // Закрываем родительский процесс

    if (setsid() < 0) exit(-1);

    pid = fork();
    if (pid < 0) exit(-1);
    if (pid > 0) exit(0);

    if (chdir("/") < 0) exit(-1);

    umask(0);

    // Перенаправляем стандартные потоки в /dev/null
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_RDWR);
}

int main(int argc, char *argv[]) {
    int is_daemon = 0;

    // Проверяем аргумент запуска
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        is_daemon = 1;
    }

    // Открываем логгер syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Настраиваем перехват сигналов SIGINT и SIGTERM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) != 0 || sigaction(SIGTERM, &sa, NULL) != 0) {
        syslog(LOG_ERR, "Failed to setup signal handler");
        closelog();
        return -1;
    }

    // 1. Создаем сокет
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
    	server_fd = socket(AF_INET, SOCK_STREAM, 0);
    	if (server_fd < 0) {
        syslog(LOG_ERR, "Socket creation failed: %m");
        closelog();
        return -1;
    }

    // 2. Включаем SO_REUSEADDR, чтобы порт не зависал в состоянии TIME_WAIT
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        syslog(LOG_ERR, "Setsockopt failed: %m");
        close(server_fd);
        closelog();
        return -1;
    }

    // Настраиваем структуру адреса
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 3. Привязываем сокет к порту 9000
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        syslog(LOG_ERR, "Bind failed: %m");
        close(server_fd);
        closelog();
        return -1;
    }

    // ДЕМОНИЗАЦИЯ: строго ПОСЛЕ успешного bind()
    if (is_daemon) {
        syslog(LOG_INFO, "Running in daemon mode");
        daemonize();
    }

    // 4. Переводим в режим прослушивания
    if (listen(server_fd, 10) < 0) {
        syslog(LOG_ERR, "Listen failed: %m");
        close(server_fd);
        closelog();
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Основной бесконечный цикл обработки подключений
    while (keep_running) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (errno == EINTR) break; // Прервано сигналом завершения
            syslog(LOG_ERR, "Accept failed: %m");
            continue;
        }

        // Преобразуем IP-адрес клиента в строку для логов
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // Буфер для накопления текущего пакета данных
        size_t buf_size = BUFFER_SIZE;
        char *recv_buf = malloc(buf_size);
        if (!recv_buf) {
            syslog(LOG_ERR, "Malloc failed");
            close(client_fd);
            continue;
        }
        size_t data_len = 0;

	// Цикл чтения данных от конкретного клиента
	while (keep_running) {
		char chunk[BUFFER_SIZE];
		ssize_t bytes_read = recv(client_fd, chunk, sizeof(chunk), 0);

		if (bytes_read < 0) {
	        	if (errno == EINTR) {
	            		continue; // Сигнал прервал чтение (нормально под Valgrind), пробуем снова
	        	}
			syslog(LOG_ERR, "Recv failed: %m");
			break; // Реальная ошибка чтения
		} else if (bytes_read == 0) {
			break; // Клиент штатно закрыл соединение
		}
            // Динамически расширяем память, если буфер заполнен
            if (data_len + bytes_read >= buf_size) {
                buf_size += bytes_read + BUFFER_SIZE;
                char *new_buf = realloc(recv_buf, buf_size);
                if (!new_buf) {
                    syslog(LOG_ERR, "Realloc failed");
                    break;
                }
                recv_buf = new_buf;
            }

            memcpy(recv_buf + data_len, chunk, bytes_read);
            data_len += bytes_read;

            // Проверяем, пришел ли символ новой строки \n (пакет завершен)
            if (memchr(chunk, '\n', bytes_read) != NULL) {
                // Записываем накопленные данные в файл
                FILE *f = fopen(DATA_FILE, "a");
                if (f) {
                    fwrite(recv_buf, 1, data_len, f);
                    fclose(f);
                }

                // Читаем весь файл построчно и отправляем обратно клиенту
                f = fopen(DATA_FILE, "r");
                if (f) {
                    char send_buf[BUFFER_SIZE];
                    size_t bytes_to_send;
                    while ((bytes_to_send = fread(send_buf, 1, sizeof(send_buf), f)) > 0) {
                        send(client_fd, send_buf, bytes_to_send, 0);
                    }
                    fclose(f);
                }
                // Сбрасываем счетчик буфера для следующего пакета
                data_len = 0;
            }
        }

        // Чистим ресурсы текущего соединения
        free(recv_buf);
        close(client_fd);
        client_fd = -1;
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    // Финальная очистка при выходе из цикла программы
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE); // Удаляем временный файл данных
    syslog(LOG_INFO, "Server shutdown complete");
    closelog();

    return 0;
}
