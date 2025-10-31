// Camada de rede: abre socket, aceita conexões e trata cada cliente.
// HTTP: aceita GET e delega o mapeamento/retorno para fs.c.

#include "http.h"
#include "fs.h"
#include "util.h"

#include <arpa/inet.h>   
#include <netinet/in.h>  
#include <stdio.h>       
#include <string.h>      
#include <sys/socket.h>  
#include <sys/types.h>
#include <sys/stat.h>    
#include <unistd.h>      
#include <limits.h>      
#include <stdlib.h>      

#define BACKLOG 16        // fila de conexões pendentes
#define RECV_BUF 4096     // buffer para ler a primeira linha do request

// Trata um cliente: lê a primeira linha, valida método e chama a camada de FS.
static void handle_client(int cfd, const char *root_real) {
    char buf[RECV_BUF + 1];

    // Lê até RECV_BUF bytes (suficiente para "GET /path HTTP/1.1\r\n")
    ssize_t n = recv(cfd, buf, RECV_BUF, 0);
    if (n <= 0) { close(cfd); return; }
    buf[n] = '\0';

    // Parse da primeira linha: MÉTODO, URL e VERSÃO
    char method[16], url[1024], version[16];
    if (sscanf(buf, "%15s %1023s %15s", method, url, version) < 2) {
        util_send_400(cfd);
        close(cfd);
        return;
    }

    // Aceita apenas GET (didático)
    if (strcmp(method, "GET") != 0) {
        util_send_405(cfd);
        close(cfd);
        return;
    }

    // ---- Query string -----------------------------------------------------
    // Mantemos a query para detectar "?list=1" (usada pelo index.html).
    // Depois de checar, removemos a query da URL para mapear o caminho.
    int want_list = 0;
    char *q = strchr(url, '?');
    if (q) {
        // procura "list=1" simples (não fazemos parse completo, didático)
        if (strstr(q + 1, "list=1") != NULL) want_list = 1;
        *q = '\0'; // remove query para resolver o caminho físico
    }

    // Decodifica %XX
    util_url_decode(url);

    // Constrói o caminho de arquivo de forma segura (sem permitir "..")
    char fs_path[PATH_MAX];
    if (!fs_join_and_sanitize(root_real, url, fs_path)) {
        util_send_404(cfd);
        close(cfd);
        return;
    }

    // Se pediram "?list=1", e o alvo é um diretório, responde JSON com a lista
    if (want_list) {
        struct stat st;
        if (stat(fs_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            fs_send_dir_json(cfd, fs_path);  // lista sem "index.html" e sem ocultos
        } else {
            util_send_404(cfd);
        }
        close(cfd);
        return;
    }

    // Responde: arquivo (com MIME) ou diretório (index.html / listagem HTML)
    fs_serve_path(cfd, url, fs_path);

    // Encerra a conexão (Connection: close)
    close(cfd);
}

// Sobe o servidor: cria socket TCP, bind/listen e atende no loop principal.
int http_run(const char *root, int port) {
    // 1) Cria o socket TCP/IPv4
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    // 2) Permite reusar a porta rapidamente após reiniciar o servidor
    int yes = 1;
    (void)setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // 3) Endereço de escuta: 0.0.0.0:<port>
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sfd); return 1;
    }
    if (listen(sfd, BACKLOG) < 0) {
        perror("listen"); close(sfd); return 1;
    }

    // 4) Resolve a raiz do site para caminho absoluto (ex.: "./files" -> "/abs/.../files")
    char root_real[PATH_MAX];
    if (!realpath(root, root_real)) {
        perror("realpath"); close(sfd); return 1;
    }

    printf("Servidor ouvindo em http://0.0.0.0:%d\n", port);
    printf("Servindo diretório: %s\n", root_real);

    // 5) Loop principal: aceita conexões e delega para handle_client
    while (1) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr *)&cli, &clilen);
        if (cfd < 0) { perror("accept"); continue; }
        handle_client(cfd, root_real);
    }

    // (em prática não chega aqui; se chegar, fecha o socket de escuta)
    close(sfd);
    return 0;
}