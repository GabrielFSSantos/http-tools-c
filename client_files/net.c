#define _POSIX_C_SOURCE 200809L
#include "net.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>

int tcp_connect(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%d", port);

    struct addrinfo hints, *res = NULL, *rp;
    memset(&hints, 0, sizeof hints);
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host, portstr, &hints, &res);
    if (err) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd; // -1 se falhou
}

ssize_t recv_line(int fd, char *buf, size_t max) {
    size_t i = 0;
    while (i + 1 < max) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r <= 0) return r;
        buf[i++] = c;
        if (i >= 2 && buf[i-2] == '\r' && buf[i-1] == '\n') {
            buf[i] = '\0';
            return (ssize_t)i;
        }
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}