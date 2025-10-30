#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>     
#include <strings.h>    
#include <sys/socket.h>
#include <sys/stat.h>   
#include <sys/types.h>
#include <unistd.h>

#define RECV_BUF     16384
#define DOWNLOAD_DIR "downloads"

// Remove \r\n do fim de uma linha
static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}

// Faz o parse de "http://host[:porta]/path"
static bool parse_url(const char *url, char *host, size_t hostsz, int *port, char *path, size_t pathsz) {
    const char *pfx = "http://";
    size_t pfxn = strlen(pfx);
    if (strncmp(url, pfx, pfxn) != 0) { fprintf(stderr, "Apenas http:// suportado.\n"); return false; }

    const char *p = url + pfxn;
    const char *slash   = strchr(p, '/');
    const char *hostend = slash ? slash : (url + strlen(url));
    const char *col     = memchr(p, ':', hostend - p);

    if (col) {
        size_t hn = (size_t)(col - p);
        if (hn == 0 || hn >= hostsz) return false;
        memcpy(host, p, hn); host[hn] = '\0';
        *port = atoi(col + 1);
        if (*port <= 0 || *port > 65535) return false;
    } else {
        size_t hn = (size_t)(hostend - p);
        if (hn == 0 || hn >= hostsz) return false;
        memcpy(host, p, hn); host[hn] = '\0';
        *port = 80;
    }

    if (slash) {
        if (strlen(slash) >= pathsz) return false;
        strncpy(path, slash, pathsz);
        path[pathsz - 1] = '\0';
    } else {
        snprintf(path, pathsz, "/");
    }
    return true;
}

// Conecta em host:port (IPv4/IPv6)
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

// Nome do arquivo a partir da path (sem query/fragment)
static const char* pick_filename(const char *path) {
    const char *p = strrchr(path, '/');
    const char *name = p ? p + 1 : path;
    static char fname[256];
    size_t n = strcspn(name, "?#");
    if (n == 0) snprintf(fname, sizeof fname, "index.html");
    else {
        if (n >= sizeof fname) n = sizeof fname - 1;
        memcpy(fname, name, n); fname[n] = '\0';
    }
    if (fname[0] == '\0') snprintf(fname, sizeof fname, "index.html");
    return fname;
}

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

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s http://host[:porta]/caminho\n", argv[0]);
        return 1;
    }

    char host[256], path[2048];
    int port;
    if (!parse_url(argv[1], host, sizeof host, &port, path, sizeof path)) {
        fprintf(stderr, "URL inválida.\n");
        return 1;
    }

    int fd = tcp_connect(host, port);
    if (fd < 0) { perror("connect"); return 1; }

    // Envia GET preservando query (parte a partir da '/')
    const char *path_only = strchr(argv[1] + strlen("http://") + strlen(host), '/');
    if (!path_only) path_only = "/";

    char req[4096];
    int n = snprintf(req, sizeof req,
        "GET %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "User-Agent: c-http-client/1.0\r\n"
        "Connection: close\r\n\r\n",
        path_only, host, port);
    if (send(fd, req, (size_t)n, 0) != n) { perror("send"); close(fd); return 1; }

    // ---- Status line ----
    char line[4096];
    ssize_t r = recv_line(fd, line, sizeof line);
    if (r <= 0) { fprintf(stderr, "Falha ao ler resposta.\n"); close(fd); return 1; }

    int status = 0;
    if (sscanf(line, "HTTP/%*s %d", &status) != 1) {
        fprintf(stderr, "Resposta HTTP inválida: %s", line);
        close(fd);
        return 1;
    }

    // ---- Cabeçalhos ----
    long content_length = -1;
    bool chunked = false;

    while ((r = recv_line(fd, line, sizeof line)) > 0) {
        if (strcmp(line, "\r\n") == 0) break;  // fim dos headers
        if (strncasecmp(line, "Content-Length:", 15) == 0) {
            content_length = strtol(line + 15, NULL, 10);
        } else if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) {
            if (strstr(line + 18, "chunked")) chunked = true;
        }
    }

    if (status != 200) {
        fprintf(stderr, "Servidor respondeu com status %d. Abortando.\n", status);
        close(fd);
        return 2;
    }

    // ---- Garante diretório ./downloads ----
    if (mkdir(DOWNLOAD_DIR, 0777) != 0 && errno != EEXIST) {
        perror("mkdir downloads");
        close(fd);
        return 1;
    }

    // ---- Arquivo de saída ----
    const char *outfile = pick_filename(path);
    char outpath[1024];
    snprintf(outpath, sizeof outpath, "%s/%s", DOWNLOAD_DIR, outfile);

    FILE *out = fopen(outpath, "wb");
    if (!out) { perror("fopen"); close(fd); return 1; }

    // ---- Corpo ----
    if (chunked) {
        // Transfer-Encoding: chunked
        for (;;) {
            if ((r = recv_line(fd, line, sizeof line)) <= 0) { fprintf(stderr, "Erro em chunk size.\n"); break; }
            trim_crlf(line);
            long chunk = strtol(line, NULL, 16);
            if (chunk <= 0) {
                // Consome CRLF final (ignorando trailers)
                recv_line(fd, line, sizeof line);
                break;
            }
            long rem = chunk;
            char buf[RECV_BUF];
            while (rem > 0) {
                ssize_t want = rem < (long)sizeof buf ? rem : (long)sizeof buf;
                ssize_t got = recv(fd, buf, (size_t)want, 0);
                if (got <= 0) { fprintf(stderr, "Erro lendo chunk.\n"); rem = 0; break; }
                fwrite(buf, 1, (size_t)got, out);
                rem -= got;
            }
            // CRLF após cada chunk
            if (recv(fd, line, 2, 0) != 2) { fprintf(stderr, "Erro pós-chunk.\n"); break; }
        }
    } else if (content_length >= 0) {
        // Content-Length conhecido
        long rem = content_length;
        char buf[RECV_BUF];
        while (rem > 0) {
            ssize_t want = rem < (long)sizeof buf ? rem : (long)sizeof buf;
            ssize_t got = recv(fd, buf, (size_t)want, 0);
            if (got <= 0) break;
            fwrite(buf, 1, (size_t)got, out);
            rem -= got;
        }
    } else {
        // Sem tamanho: lê até EOF
        char buf[RECV_BUF];
        while ((r = recv(fd, buf, sizeof buf, 0)) > 0) {
            fwrite(buf, 1, (size_t)r, out);
        }
    }

    fclose(out);
    close(fd);
    printf("Salvo em: %s\n", outpath);
    return 0;
}