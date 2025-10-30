#include "server_files/http.h"
#include <stdio.h>   
#include <stdlib.h> 
#include <sys/stat.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s [<raiz>] [<porta>]\n"
        "Ex.: %s ./files 5050\n"
        "Se omitidos: raiz=./files, porta=5050\n",
        prog, prog);
}

int main(int argc, char **argv) {
    const char *root = (argc >= 2) ? argv[1] : "./files";
    int port = (argc >= 3) ? atoi(argv[2]) : 5050;

    if (argc >= 2 && (argv[1][0] == '-' && argv[1][1] == 'h')) {
        print_usage(argv[0]);
        return 0;
    }

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Porta inválida: %d\n", port);
        print_usage(argv[0]);
        return 1;
    }

    printf("Raiz servida: %s\n", root);
    printf("Porta: %d\n", port);
    return http_run(root, port);
}