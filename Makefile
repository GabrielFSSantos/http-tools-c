# Makefile simples: compila servidor e cliente.

CC      = gcc
CFLAGS  = -Wall -Wextra -O2

# --- Servidor ---
SERVER_SRCS = server.c \
              server_files/http.c \
              server_files/fs.c \
              server_files/util.c
SERVER_OBJS = $(SERVER_SRCS:.c=.o)
SERVER_BIN  = server

# --- Cliente ---
CLIENT_SRCS = client.c \
              client_files/url.c \
              client_files/http_client.c \
              client_files/json_list.c \
              client_files/io.c
CLIENT_OBJS = $(CLIENT_SRCS:.c=.o)
CLIENT_BIN  = client

.PHONY: all clean run run-client

all: $(SERVER_BIN) $(CLIENT_BIN)

$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SERVER_OBJS)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CLIENT_OBJS)

clean:
	rm -f $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_BIN) $(CLIENT_BIN)

# Auxiliares de execução (ajuste o diretório conforme preferir)
run: $(SERVER_BIN)
	./server ./files 5050

run-client: $(CLIENT_BIN)
	./client http://localhost:5050/