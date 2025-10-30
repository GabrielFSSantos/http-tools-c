# Makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -O2
PORT   ?= 5050

SERVER_SRCS = server.c server_files/http.c server_files/fs.c server_files/util.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)

all: server

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

client: client.c
	$(CC) $(CFLAGS) -o $@ $<

run: server
	./server ./files $(PORT)

clean:
	rm -f $(SERVER_OBJS) server client

.PHONY: all run clean