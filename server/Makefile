# Автоматически выбираем компилятор (поддерживает кросс-компиляцию)
CC ?= $(CROSS_COMPILE)gcc
# Включаем строгие предупреждения -Wall и -Werror, как просил преподаватель
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?=

# Цель по умолчанию (all)
all: aesdsocket

# Правило сборки исполняемого файла
aesdsocket: aesdsocket.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Правило для очистки временных файлов
clean:
	rm -f aesdsocket *.o
