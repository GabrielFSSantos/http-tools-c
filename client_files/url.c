// Implementação de parse/encoding de URL.

#define _POSIX_C_SOURCE 200809L
#include "url.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Deduz nome do arquivo a partir da path (sem query/fragment)
const char* pick_filename(const char *path) {
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

// Faz o parse de "http://host[:porta]/path"
bool parse_url(const char *url, char *host, size_t hostsz, int *port, char *path, size_t pathsz) {
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

// Codifica um segmento de path para URL (espaços, acentos, etc. -> %XX)
void url_encode_segment(const char *in, char *out, size_t outsz) {
    static const char *hex = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < outsz; i++) {
        unsigned char c = (unsigned char)in[i];
        // "unreserved" (RFC 3986)
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

// Reconstrói a URL com a *path* devidamente encodada por segmentos.
bool encode_path_of_url(const char *in_url, char *out_url, size_t outsz) {
    char host[256], path[2048]; int port;
    if (!parse_url(in_url, host, sizeof host, &port, path, sizeof path)) return false;

    int n = snprintf(out_url, outsz, "http://%s:%d", host, port);
    if (n <= 0 || (size_t)n >= outsz) return false;

    const char *p = path;
    // Garante que começaremos exatamente com a estrutura de segments
    while (*p) {
        // adiciona '/' (entre segmentos)
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