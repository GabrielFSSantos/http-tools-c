// Responsável por mapear URL -> caminho no disco e responder:
// - arquivo regular  -> envia arquivo (com Content-Type básico)
// - diretório        -> tenta <dir>/index.html; senão, lista conteúdo
// - inexistente/outro-> 404 simples

#include "fs.h"
#include "util.h"

#include <dirent.h>         
#include <fcntl.h>          
#include <limits.h>         
#include <stdbool.h>        
#include <stdio.h>          
#include <stdlib.h>         
#include <string.h>         
#include <sys/socket.h>     
#include <sys/stat.h>       
#include <unistd.h>         

// -----------------------------------------------------------------------------
// Listagem HTML.
// -----------------------------------------------------------------------------
static void send_dir_listing(int fd, const char *url_path, const char *fs_dir) {
    DIR *d = opendir(fs_dir);
    if (!d) { util_send_404(fd); return; }

    size_t cap = 4096, len = 0;
    char *body = (char *)malloc(cap);
    if (!body) { closedir(d); return; }

    len += snprintf(body + len, cap - len,
        "<!doctype html><meta charset='utf-8'>"
        "<title>Index of %s</title><h1>Index of %s</h1><ul>",
        url_path, url_path);

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) continue;

        char item[PATH_MAX];
        snprintf(item, sizeof(item), "%s/%s",
                 (*url_path ? url_path : ""), ent->d_name);

        // reserva espaço (64 de folga p/ tags)
        size_t need = strlen(ent->d_name) + strlen(item) + 64;
        if (len + need >= cap) {
            size_t novo = cap * 2;
            if (novo < len + need + 1024) novo = len + need + 1024;
            char *tmp = (char *)realloc(body, novo);
            if (!tmp) { free(body); closedir(d); return; }
            body = tmp; cap = novo;
        }

        len += snprintf(body + len, cap - len,
                        "<li><a href=\"/%s\">%s</a></li>", item, ent->d_name);
    }
    closedir(d);

    if (len + 6 >= cap) {
        char *tmp = (char *)realloc(body, cap + 16);
        if (!tmp) { free(body); return; }
        body = tmp; cap += 16;
    }
    len += snprintf(body + len, cap - len, "</ul>");

    util_send_headers(fd, "200 OK", "text/html; charset=utf-8", (long)len);
    (void)send(fd, body, len, 0);
    free(body);
}

// -----------------------------------------------------------------------------
// Envia arquivo usando loop read()/send() (simples e portátil).
// -----------------------------------------------------------------------------
static void send_file(int fd, const char *fs_path) {
    int f = open(fs_path, O_RDONLY);
    if (f < 0) { util_send_404(fd); return; }

    struct stat st;
    if (fstat(f, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(f); util_send_404(fd); return;
    }

    const char *ctype = util_mime_type(fs_path);
    util_send_headers(fd, "200 OK", ctype, (long)st.st_size);

    char buf[16 * 1024];
    ssize_t n;
    while ((n = read(f, buf, sizeof(buf))) > 0) {
        ssize_t sent_total = 0;
        while (sent_total < n) {
            ssize_t s = send(fd, buf + sent_total, (size_t)(n - sent_total), 0);
            if (s <= 0) { close(f); return; }
            sent_total += s;
        }
    }
    close(f);
}

// -----------------------------------------------------------------------------
// Mapeia URL -> caminho seguro em disco (sem permitir "..") e entrega em out_path.
// -----------------------------------------------------------------------------
bool fs_join_and_sanitize(const char *root, const char *url_path, char out_path[]) {
    // 1) normaliza componentes da URL
    char clean[PATH_MAX] = "";
    const char *p = url_path;
    if (*p == '/') p++; // ignora barra inicial
    while (*p) {
        const char *q = strchr(p, '/');
        size_t len = q ? (size_t)(q - p) : strlen(p);
        if (len == 0) { p++; continue; }
        if (len == 1 && p[0] == '.') {
            // ignora "."
        } else if (len == 2 && p[0] == '.' && p[1] == '.') {
            return false; // recusa ".."
        } else {
            if (clean[0]) strncat(clean, "/", sizeof(clean) - strlen(clean) - 1);
            strncat(clean, p, len);
        }
        if (!q) break;
        p = q + 1;
    }

    // 2) resolve raiz para caminho absoluto
    char root_real[PATH_MAX];
    if (!realpath(root, root_real)) return false;

    // 3) monta caminho final
    char resolved[PATH_MAX];
    if (clean[0]) {
        if (snprintf(resolved, sizeof(resolved), "%s/%s", root_real, clean) >= (int)sizeof(resolved))
            return false;
    } else {
        if (snprintf(resolved, sizeof(resolved), "%s", root_real) >= (int)sizeof(resolved))
            return false;
    }

    // 4) garante que resolved permanece sob root_real
    size_t rl = strlen(root_real);
    if (strncmp(resolved, root_real, rl) != 0 ||
        (resolved[rl] != '/' && resolved[rl] != '\0')) {
        return false;
    }

    // 5) entrega saída
    strncpy(out_path, resolved, PATH_MAX - 1);
    out_path[PATH_MAX - 1] = '\0';
    return true;
}

// -----------------------------------------------------------------------------
// Decide resposta para o caminho dado: index.html (se dir), listagem ou arquivo.
// -----------------------------------------------------------------------------
void fs_serve_path(int fd, const char *url_path, const char *fs_path) {
    struct stat st;
    if (stat(fs_path, &st) != 0) { util_send_404(fd); return; }

    if (S_ISDIR(st.st_mode)) {
        char idx[PATH_MAX];
        snprintf(idx, sizeof(idx), "%s/index.html", fs_path);

        if (stat(idx, &st) == 0 && S_ISREG(st.st_mode)) {
            send_file(fd, idx);
        } else {
            // remove "/" final só para apresentação
            char u[PATH_MAX];
            snprintf(u, sizeof(u), "%s", url_path);
            size_t ul = strlen(u);
            if (ul > 1 && u[ul - 1] == '/') u[ul - 1] = '\0';
            send_dir_listing(fd, u, fs_path);
        }
    } else if (S_ISREG(st.st_mode)) {
        send_file(fd, fs_path);
    } else {
        util_send_404(fd);
    }
}