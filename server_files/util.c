// Utilitários do servidor: MIME type, URL decode e respostas HTTP básicas.

#include "util.h"

#include <ctype.h>      
#include <stdarg.h>     
#include <stdbool.h>
#include <stdio.h>      
#include <stdlib.h>     
#include <string.h>     
#include <strings.h>    
#include <sys/socket.h> 

#define SEND_BUF 8192

// -----------------------------------------------------------------------------
// Helper: envia uma string formatada direto no socket (printf-like).
// -----------------------------------------------------------------------------
static void sendf(int fd, const char *fmt, ...) {
    char buf[SEND_BUF];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) (void)send(fd, buf, (size_t)n, 0);
}

// -----------------------------------------------------------------------------
// Deduz um Content-Type simples pela extensão do arquivo.
// -----------------------------------------------------------------------------
const char *util_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    ext++; // pula o ponto

    if (!strcasecmp(ext, "html") || !strcasecmp(ext, "htm")) return "text/html; charset=utf-8";
    if (!strcasecmp(ext, "css"))  return "text/css; charset=utf-8";
    if (!strcasecmp(ext, "js"))   return "application/javascript; charset=utf-8";
    if (!strcasecmp(ext, "json")) return "application/json; charset=utf-8";
    if (!strcasecmp(ext, "txt"))  return "text/plain; charset=utf-8";
    if (!strcasecmp(ext, "png"))  return "image/png";
    if (!strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg")) return "image/jpeg";
    if (!strcasecmp(ext, "gif"))  return "image/gif";
    if (!strcasecmp(ext, "svg"))  return "image/svg+xml";
    if (!strcasecmp(ext, "pdf"))  return "application/pdf";
    if (!strcasecmp(ext, "ico"))  return "image/x-icon";

    return "application/octet-stream";
}

// -----------------------------------------------------------------------------
// Decodifica %XX em uma URL (forma simples; não converte '+').
// -----------------------------------------------------------------------------
void util_url_decode(char *s) {
    char *o = s;
    for (; *s; s++, o++) {
        if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
            char hex[3] = { s[1], s[2], 0 };
            *o = (char)strtol(hex, NULL, 16);
            s += 2;
        } else {
            *o = *s;
        }
    }
    *o = '\0';
}

// -----------------------------------------------------------------------------
// Envia cabeçalhos HTTP básicos e a linha em branco final.
// -----------------------------------------------------------------------------
void util_send_headers(int fd, const char *status, const char *ctype, long content_length) {
    sendf(fd,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n\r\n",
        status, ctype, content_length);
}

// -----------------------------------------------------------------------------
// 400 Bad Request: request inicial inválido.
// -----------------------------------------------------------------------------
void util_send_400(int fd) {
    const char *body =
        "<!doctype html><meta charset='utf-8'><title>400</title>"
        "<h1>400 - Bad Request</h1>";
    util_send_headers(fd, "400 Bad Request", "text/html; charset=utf-8", (long)strlen(body));
    (void)send(fd, body, strlen(body), 0);
}

// -----------------------------------------------------------------------------
// 404 Not Found: rota/caminho não encontrado.
// -----------------------------------------------------------------------------
void util_send_404(int fd) {
    const char *body =
        "<!doctype html><meta charset='utf-8'><title>404</title>"
        "<h1>404 - Not Found</h1><p>Recurso não encontrado.</p>";
    util_send_headers(fd, "404 Not Found", "text/html; charset=utf-8", (long)strlen(body));
    (void)send(fd, body, strlen(body), 0);
}

// -----------------------------------------------------------------------------
// 405 Method Not Allowed
// -----------------------------------------------------------------------------
void util_send_405(int fd) {
    const char *body =
        "<!doctype html><meta charset='utf-8'><title>405</title>"
        "<h1>405 - Method Not Allowed</h1>";
    sendf(fd,
        "HTTP/1.1 405 Method Not Allowed\r\n"
        "Allow: GET\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",
        strlen(body), body);
}