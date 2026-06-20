CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?=

all: writer

writer: writer.c
	$(CC) $(CFLAGS) writer.c -o writer $(LDFLAGS)

clean:
	rm -f writer *.o
