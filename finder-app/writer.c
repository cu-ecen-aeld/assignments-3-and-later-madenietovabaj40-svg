#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    // Открываем логгер aesdsocket/writer
    openlog("writer", LOG_PID, LOG_USER);

    // Проверяем, что передано ровно 2 аргумента (имя файла и строка)
    if (argc != 3) {
        syslog(LOG_ERR, "Error: Invalid number of arguments. Expected 2, got %d", argc - 1);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    char *writefile = argv[1];
    char *writestr = argv[2];

    // Логируем операцию на уровне LOG_DEBUG
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // Открываем файл на запись
    FILE *fp = fopen(writefile, "w");
    if (fp == NULL) {
        syslog(LOG_ERR, "Error: Could not open file %s for writing", writefile);
        perror("fopen");
        closelog();
        return 1;
    }

    // Записываем строку в файл
    if (fputs(writestr, fp) == EOF) {
        syslog(LOG_ERR, "Error: Could not write string to file %s", writefile);
        fclose(fp);
        closelog();
        return 1;
    }

    // Закрываем файл и логгер
    fclose(fp);
    closelog();
    return 0;
}
