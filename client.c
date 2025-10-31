// Cliente HTTP em C (apenas HTTP, sem TLS).
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "client_files/common.h"
#include "client_files/url.h"
#include "client_files/http_client.h"
#include "client_files/json_list.h"
#include "client_files/io.h"

typedef enum { MODE_SINGLE = 0, MODE_LIST = 1, MODE_ALL = 2 } RunMode;

// Baixa um único arquivo a partir de uma URL completa (com path já OK/encodada)
static int download_one(const char *full_url) {
    char host[256], path[2048];
    int port;
    if (!parse_url(full_url, host, sizeof host, &port, path, sizeof path)) {
        fprintf(stderr, "URL inválida: %s\n", full_url);
        return 1;
    }
    const char *fname = pick_filename(path);

    if (ensure_download_dir() != 0) {
        perror("mkdir downloads");
        return 1;
    }

    char outpath[1024];
    snprintf(outpath, sizeof outpath, "%s/%s", DOWNLOAD_DIR, fname);

    int st = http_get(full_url, outpath, NULL, NULL);
    if (st == 200) {
        printf("Baixado: %s -> %s\n", full_url, outpath);
        return 0;
    }
    fprintf(stderr, "Status HTTP %d em %s\n", st, full_url);
    return 2;
}

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Uso:\n"
        "  %s http://host[:porta]/caminho           # baixa um recurso\n"
        "  %s --list http://host[:porta]/diretorio/ # lista itens (usa ?list=1)\n"
        "  %s --all  http://host[:porta]/diretorio/ # baixa todos os itens listados\n",
        prog, prog, prog);
}

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
        // Encoda automaticamente cada segmento da path (para nomes com espaço/acentos)
        char enc_url[4096];
        if (encode_path_of_url(url, enc_url, sizeof enc_url))
            return download_one(enc_url);
        else
            return download_one(url); // fallback
    }

    // Modo LIST/ALL: consome a lista JSON do servidor (?list=1)
    char list_url[4096];
    build_list_url(url, list_url, sizeof list_url);

    char *body = NULL; size_t body_len = 0;
    int st = http_get(list_url, NULL, &body, &body_len);
    if (st != 200 || !body) {
        fprintf(stderr, "Falha ao obter lista em %s\n", list_url);
        free(body);
        return 2;
    }

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

        // Constrói URL do arquivo a partir da URL do diretório informada
        char file_url[4096];
        size_t L = strlen(url);
        if (L > 0 && url[L-1] == '/') snprintf(file_url, sizeof file_url, "%s%s", url, enc);
        else                          snprintf(file_url, sizeof file_url, "%s/%s", url, enc);

        // Remove query (ex.: ?list=1) para baixar o arquivo direto
        char *q = strchr(file_url, '?');
        if (q) *q = '\0';

        int rc = download_one(file_url);
        if (rc != 0) fprintf(stderr, "Falha ao baixar: %s\n", file_url);
    }
    return 0;
}