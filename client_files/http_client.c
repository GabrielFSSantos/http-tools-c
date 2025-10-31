// Implementação de GET simples (HTTP/1.1) com suporte a Content-Length e chunked.

#define _POSIX_C_SOURCE 200809L
#include "http_client.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include "common.h"
#include "url.h"

// Lê uma linha (até CRLF) do socket
static ssize_t recv_line(int fd, char *buf, size_t max) {
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

static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}

// TCP connect (IPv4/IPv6)
static int tcp_connect(const char *host, int port) {
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

int http_get(const char *full_url, const char *save_path, char **out_body, size_t *out_len) {
    char host[256], path[2048];
    int port;
    if (!parse_url(full_url, host, sizeof host, &port, path, sizeof path)) return -1;

    int fd = tcp_connect(host, port);
    if (fd < 0) { perror("connect"); return -1; }

    // Envia GET preservando query (parte a partir da '/')
    const char *path_only = strchr(full_url + strlen("http://") + strlen(host), '/');
    if (!path_only) path_only = "/";

    char req[4096];
    int n = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: c-http-client/1.0\r\n"
        "Connection: close\r\n\r\n",
        path_only, host, port);
    if (send(fd, req, (size_t)n, 0) != n) { perror("send"); close(fd); return -1; }

    // ---- Status line ----
    char line[4096];
    ssize_t r = recv_line(fd, line, sizeof line);
    if (r <= 0) { fprintf(stderr, "Falha ao ler resposta.\n"); close(fd); return -1; }

    int status = 0;
    if (sscanf(line, "HTTP/%*s %d", &status) != 1) {
        fprintf(stderr, "Resposta HTTP inválida: %s", line);
        close(fd);
        return -1;
    }

    // ---- Cabeçalhos ----
    long content_length = -1;
    int chunked = 0;

    while ((r = recv_line(fd, line, sizeof line)) > 0) {
        if (strcmp(line, "\r\n") == 0) break;  // fim dos headers
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = strtol(line + 15, NULL, 10);
        } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
            if (strstr(line + 18, "chunked")) chunked = 1;
        }
    }

    if (status != 200) {
        close(fd);
        return status; // devolve o status HTTP para o chamador decidir
    }

    // Saída: arquivo OU memória
    FILE *out = NULL;
    char *mem = NULL;
    size_t mem_cap = 0, mem_len = 0;

    if (save_path) {
        out = fopen(save_path, "wb");
        if (!out) { perror("fopen"); close(fd); return -1; }
    }

    // ---- Corpo ----
    if (chunked) {
        // Transfer-Encoding: chunked
        for (;;) {
            if ((r = recv_line(fd, line, sizeof line)) <= 0) { fprintf(stderr, "Erro em chunk size.\n"); break; }
            trim_crlf(line);
            long chunk = strtol(line, NULL, 16);
            if (chunk <= 0) { // fim
                recv_line(fd, line, sizeof line); // consome CRLF final
                break;
            }
            long rem = chunk;
            char buf[RECV_BUF];
            while (rem > 0) {
                ssize_t want = rem < (long)sizeof buf ? rem : (long)sizeof buf;
                ssize_t got = recv(fd, buf, (size_t)want, 0);
                if (got <= 0) { fprintf(stderr, "Erro lendo chunk.\n"); rem = 0; break; }
                if (out) {
                    fwrite(buf, 1, (size_t)got, out);
                } else {
                    if (mem_len + (size_t)got + 1 > mem_cap) {
                        size_t novo = mem_cap ? mem_cap * 2 : 16384;
                        while (novo < mem_len + (size_t)got + 1) novo *= 2;
                        char *tmp = (char *)realloc(mem, novo);
                        if (!tmp) { free(mem); close(fd); if (out) fclose(out); return -1; }
                        mem = tmp; mem_cap = novo;
                    }
                    memcpy(mem + mem_len, buf, (size_t)got);
                    mem_len += (size_t)got;
                    mem[mem_len] = '\0';
                }
                rem -= got;
            }
            // CRLF após cada chunk
            if (recv(fd, line, 2, 0) != 2) { fprintf(stderr, "Erro pós-chunk.\n"); break; }
        }
    } else if (content_length >= 0) {
        long rem = content_length;
        char buf[RECV_BUF];
        while (rem > 0) {
            ssize_t want = rem < (long)sizeof buf ? rem : (long)sizeof buf;
            ssize_t got = recv(fd, buf, (size_t)want, 0);
            if (got <= 0) break;
            if (out) {
                fwrite(buf, 1, (size_t)got, out);
            } else {
                if (mem_len + (size_t)got + 1 > mem_cap) {
                    size_t novo = mem_cap ? mem_cap * 2 : 16384;
                    while (novo < mem_len + (size_t)got + 1) novo *= 2;
                    char *tmp = (char *)realloc(mem, novo);
                    if (!tmp) { free(mem); close(fd); if (out) fclose(out); return -1; }
                    mem = tmp; mem_cap = novo;
                }
                memcpy(mem + mem_len, buf, (size_t)got);
                mem_len += (size_t)got;
                mem[mem_len] = '\0';
            }
            rem -= got;
        }
    } else {
        // Sem tamanho: lê até EOF
        char buf[RECV_BUF];
        while ((r = recv(fd, buf, sizeof buf, 0)) > 0) {
            if (out) {
                fwrite(buf, 1, (size_t)r, out);
            } else {
                if (mem_len + (size_t)r + 1 > mem_cap) {
                    size_t novo = mem_cap ? mem_cap * 2 : 16384;
                    while (novo < mem_len + (size_t)r + 1) novo *= 2;
                    char *tmp = (char *)realloc(mem, novo);
                    if (!tmp) { free(mem); close(fd); if (out) fclose(out); return -1; }
                    mem = tmp; mem_cap = novo;
                }
                memcpy(mem + mem_len, buf, (size_t)r);
                mem_len += (size_t)r;
                mem[mem_len] = '\0';
            }
        }
    }

    close(fd);
    if (out) fclose(out);
    if (!out) {
        if (out_body) *out_body = mem; else free(mem);
        if (out_len)  *out_len  = mem_len;
    }
    return 200;
}