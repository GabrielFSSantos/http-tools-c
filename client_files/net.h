#pragma once
#include <sys/types.h>

// Conecta a host:port (IPv4/IPv6)
int tcp_connect(const char *host, int port);

// Lê uma linha (terminada em CRLF) do socket
ssize_t recv_line(int fd, char *buf, size_t max);

// Remove CRLF do fim da string (útil para Transfer-Encoding: chunked)
void trim_crlf(char *s);
