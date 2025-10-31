// Cliente HTTP
//
// Modos:
//   1) Baixar um único recurso:
//        ./client http://host[:porta]/caminho
//   2) Listar itens de um diretório (usa /?list=1 do servidor):
//        ./client --list http://host[:porta]/diretorio/
//   3) Baixar todos os itens listados (usa /?list=1):
//        ./client --all  http://host[:porta]/diretorio/
//
// Saídas são gravadas em ./downloads/<nome>.

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

typedef enum { MODE_SINGLE = 0, MODE_LIST = 1, MODE_ALL = 2 } RunMode;

// -----------------------------------------------------------------------------
// Utilitários
// -----------------------------------------------------------------------------

// Remove \r\n do fim de uma linha
static void trim_crlf(char *s) {
    size_t n = strlen(s);
    while (n && (s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = '\0';
}

// Codifica um segmento de path para URL (espaços, acentos, etc. -> %XX)
static void url_encode_segment(const char *in, char *out, size_t outsz) {
    static const char *hex = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < outsz; i++) {
        unsigned char c = (unsigned char)in[i];
        // "unreserved" (RFC 3986): ALPHA / DIGIT / "-" / "." / "_" / "~"
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' ||
            c == '_' || c == '~') {
            out[j++] = (char)c;
        } else {
            out[j++] = '%';
            out[j++] = hex[c >> 4];
            out[j++] = hex[c & 0x0F];
        }
    }
    out[j] = '\0';
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

// -----------------------------------------------------------------------------
// HTTP GET: salva em arquivo (save_path != NULL) OU retorna corpo em memória
// -----------------------------------------------------------------------------
static int http_get(const char *full_url, const char *save_path, char **out_body, size_t *out_len) {
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
        fprintf(stderr, "Status HTTP %d em %s\n", status, full_url);
        close(fd);
        return status;
    }

    // Saída: arquivo OU memória
    FILE *out = NULL;
    char *mem = NULL;
    size_t mem_cap = 0, mem_len = 0;

    if (save_path) {
        // Garante diretório ./downloads
        if (mkdir(DOWNLOAD_DIR, 0777) != 0 && errno != EEXIST) {
            perror("mkdir downloads"); close(fd); return -1;
        }
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
    if (!out) { if (out_body) *out_body = mem; else free(mem); if (out_len) *out_len = mem_len; }
    return 200;
}

// -----------------------------------------------------------------------------
// Helpers para listar / baixar múltiplos
// -----------------------------------------------------------------------------

// Monta URL com ?list=1 (ou &list=1 se já houver query)
static void build_list_url(const char *base_url, char *out, size_t outsz) {
    const char *q = strchr(base_url, '?');
    if (q) snprintf(out, outsz, "%s&list=1", base_url);
    else   snprintf(out, outsz, "%s?list=1", base_url);
}

// Parsing JSON MUITO simples: extrai strings entre aspas (["a","b",...])
static size_t parse_json_list(const char *json, char items[][512], size_t max_items) {
    size_t count = 0;
    const char *p = json;
    while (*p && count < max_items) {
        const char *a = strchr(p, '\"');
        if (!a) break;
        a++;
        const char *b = strchr(a, '\"');
        if (!b) break;
        size_t n = (size_t)(b - a);
        if (n >= 512) n = 511;
        memcpy(items[count], a, n);
        items[count][n] = '\0';
        count++;
        p = b + 1;
    }
    return count;
}

// Reconstrói a URL com a *path* devidamente encodada por segmentos.
// Útil para modo "um arquivo" quando o usuário passa a URL com espaços/acentos.
static bool encode_path_of_url(const char *in_url, char *out_url, size_t outsz) {
    char host[256], path[2048]; int port;
    if (!parse_url(in_url, host, sizeof host, &port, path, sizeof path)) return false;

    int n = snprintf(out_url, outsz, "http://%s:%d", host, port);
    if (n <= 0 || (size_t)n >= outsz) return false;

    const char *p = path;
    while (*p) {
        // adiciona '/'
        if ((size_t)n + 1 >= outsz) return false;
        out_url[n++] = '/';

        // pula barras consecutivas
        while (*p == '/') p++;

        // coleta um segmento
        char seg[1024]; size_t i = 0;
        while (*p && *p != '/' && i + 1 < sizeof seg) seg[i++] = *p++;
        seg[i] = '\0';

        // encoda e concatena
        char enc[2048];
        url_encode_segment(seg, enc, sizeof enc);
        size_t elen = strlen(enc);
        if ((size_t)n + elen >= outsz) return false;
        memcpy(out_url + n, enc, elen); n += (int)elen;
    }
    out_url[n] = '\0';
    return true;
}

// Baixa um único arquivo a partir de uma URL completa
static int download_one(const char *full_url) {
    char host[256], path[2048];
    int port;
    if (!parse_url(full_url, host, sizeof host, &port, path, sizeof path)) {
        fprintf(stderr, "URL inválida: %s\n", full_url); return 1;
    }
    const char *fname = pick_filename(path);
    char outpath[1024]; snprintf(outpath, sizeof outpath, "%s/%s", DOWNLOAD_DIR, fname);
    int st = http_get(full_url, outpath, NULL, NULL);
    if (st == 200) printf("Baixado: %s -> %s\n", full_url, outpath);
    return st == 200 ? 0 : 2;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s http://host[:porta]/caminho           # baixa um recurso\n"
        "  %s --list http://host[:porta]/diretorio/ # lista itens (usa ?list=1)\n"
        "  %s --all  http://host[:porta]/diretorio/ # baixa todos os itens listados\n",
        prog, prog, prog);
}

// -----------------------------------------------------------------------------
// main: roteamento dos modos
// -----------------------------------------------------------------------------
int main(int argc, char **argv) {
    RunMode mode = MODE_SINGLE;
    const char *url = NULL;

    if (argc == 2) {
        // modo SINGLE: baixa um recurso
        url = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--list") == 0) {
        mode = MODE_LIST; url = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--all") == 0) {
        mode = MODE_ALL; url = argv[2];
    } else {
        print_usage(argv[0]);
        return 1;
    }

    if (mode == MODE_SINGLE) {
        // Novo: encodar automaticamente a *path* da URL por segmentos
        char enc_url[4096];
        if (encode_path_of_url(url, enc_url, sizeof enc_url))
            return download_one(enc_url);
        else
            return download_one(url); // fallback (não deve acontecer em condições normais)
    }

    // Modo LIST/ALL: chama ?list=1 e processa JSON
    char list_url[4096];
    build_list_url(url, list_url, sizeof list_url);

    char *body = NULL; size_t body_len = 0;
    int st = http_get(list_url, NULL, &body, &body_len);
    if (st != 200 || !body) {
        fprintf(stderr, "Falha ao obter lista em %s\n", list_url);
        free(body);
        return 2;
    }

    // parse JSON simples
    char items[1024][512];
    size_t n = parse_json_list(body, items, 1024);
    free(body);

    if (n == 0) {
        printf("(lista vazia)\n");
        return 0;
    }

    if (mode == MODE_LIST) {
        for (size_t i = 0; i < n; i++) printf("%s\n", items[i]);
        return 0;
    }

    // MODE_ALL: baixar todos (codificando cada nome para URL)
    for (size_t i = 0; i < n; i++) {
        char enc[2048];
        url_encode_segment(items[i], enc, sizeof enc);

        // Constrói URL do arquivo a partir da URL do diretório
        char file_url[4096];
        size_t L = strlen(url);
        if (L > 0 && url[L-1] == '/') snprintf(file_url, sizeof file_url, "%s%s", url, enc);
        else                          snprintf(file_url, sizeof file_url, "%s/%s", url, enc);

        // Remove query da URL base, para baixar direto o arquivo
        char *q = strchr(file_url, '?');
        if (q) *q = '\0';

        int rc = download_one(file_url);
        if (rc != 0) fprintf(stderr, "Falha ao baixar: %s\n", file_url);
    }
    return 0;
}