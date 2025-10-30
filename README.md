# http-tools-c

Implementação **didática** de um servidor HTTP/1.1 mínimo em C (apenas **GET**) e um **cliente** HTTP simples para download. O objetivo é entender melhor redes + HTTP na prática.

> **Stack**: C99 (gcc), sockets Berkeley, POSIX; sem dependências externas.

## ✨ Funcionalidades

* Servidor HTTP:

  * Responde **GET** a arquivos e diretórios dentro de uma **raiz** (document root).
  * Determina **Content-Type** pelo sufixo de arquivo (html, css, js, png, jpg, svg, pdf, txt…).
  * Lista diretórios com uma **página de índice simples** quando não há `index.html`.
  * Encaminha `index.html` quando a URL aponta para um diretório.
  * Respostas básicas: `200 OK`, `404 Not Found`, `400 Bad Request` (erro de parsing) e `405 Method Not Allowed` (para métodos ≠ GET).
* Cliente HTTP:

  * Faz **GET** de uma URL `http://host[:porta]/caminho` e salva em `./downloads/<arquivo>`.
  * Entende `Content-Length` e **Transfer-Encoding: chunked** (decodificação de corpo por chunks). ([IETF Datatracker][2])

> A estrutura de diretórios do repositório (padrão) já traz `server.c`, `client.c`, `server_files/`, `files/` e `downloads/`. ([GitHub][1])

---

## 🗂️ Estrutura

```
.
├─ server.c                 # main do servidor (argumentos, chamada http_run)
├─ client.c                 # cliente HTTP (GET e grava em ./downloads)
├─ server_files/
│  ├─ http.c  http.h        # socket, bind/listen/accept, parsing da 1ª linha e roteamento
│  ├─ fs.c    fs.h          # junção segura raiz + URL, envio de arquivo e listagem de diretório
│  └─ util.c  util.h        # MIME types, URL decode, cabeçalhos e respostas 400/404/405
├─ files/                   # “document root” padrão do servidor (você coloca seus arquivos aqui)
├─ downloads/               # saída padrão de downloads do cliente
├─ Makefile                 # alvos: server (padrão), client, run, clean
└─ README.md
```

---

## ✅ Pré-requisitos

* Linux/WSL, `gcc` e `make` instalados.
* Porta disponível (padrão **5050**).

---

## 🔧 Compilar

```bash
# compila o servidor (gera ./server)
make

# compila o cliente (gera ./client)
make client
```

---

## ▶️ Executar o servidor

Coloque os arquivos que deseja servir em `./files` (ou informe outra raiz).

```bash
# cria um exemplo mínimo
mkdir -p ./files
echo "olá" > ./files/arquivo.txt
```

Suba o servidor (padrão: raiz `./files`, porta `5050`):

```bash
./server
# ou escolhendo raiz e porta:
./server ./files 5050
```

Abra no navegador:
`http://localhost:5050/`

---

## ⬇️ Usar o cliente (downloader)

Baixa o recurso e salva em `./downloads/<nome>`:

```bash
# baixe a página inicial do seu server
./client http://localhost:5050/

# baixe um arquivo específico
./client http://localhost:5050/arquivo.txt

# ver o que foi salvo
ls -lh downloads/
```

O cliente suporta corpo com `Content-Length` e **transferência “chunked”**; a detecção é feita pelos cabeçalhos HTTP da resposta. ([MDN Web Docs][3])

---

## 🛠️ Alvos úteis do Makefile

```bash
make           # compila o servidor (binário: ./server)
make client    # compila o cliente  (binário: ./client)
make clean     # remove binários e objetos
```

---

## 🔒 Notas de segurança

* O servidor **limpa e normaliza** o caminho da URL e **recusa “..”**, juntando com a raiz resolvida por `realpath` para evitar “escape” do diretório publicado (Directory Traversal).
* Apenas método **GET** é aceito; qualquer outro recebe `405 Method Not Allowed`.
* Não há TLS/HTTPS, autenticação, compressão, cache, HTTP/2 etc.

Para a sintaxe/mensagens do HTTP/1.1, consulte as RFCs (abaixo). ([IETF Datatracker][4])

---

## 📚 Referências

* **HTTP/1.1 – Message Syntax and Routing** (RFC 7230). Base canônica para a gramática de mensagens HTTP/1.1. ([IETF Datatracker][4])
* **HTTP/1.1 – Semantics and Content** (RFC 7231). Métodos, códigos de status e cabeçalhos semânticos. ([IETF Datatracker][5])
* **HTTP/1.1 – Chunked Transfer Coding** (seção 7.1 da RFC 9112) e guia da MDN para `Transfer-Encoding`. Úteis para o cliente que decodifica **chunked**. ([IETF Datatracker][2])
* **Repositório**: *http-tools-c* (estrutura e código). ([GitHub][1])

---

## 📜 Licença

Uso acadêmico/didático. (Adapte aqui a licença que preferir: MIT, BSD, etc.)

[1]: https://github.com/GabrielFSSantos/http-tools-c "GitHub - GabrielFSSantos/http-tools-c: Implementação didática do protocolo HTTP em C"
[2]: https://datatracker.ietf.org/doc/html/rfc9112?utm_source=chatgpt.com "RFC 9112 - HTTP/1.1"
[3]: https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Transfer-Encoding?utm_source=chatgpt.com "Transfer-Encoding header - HTTP - MDN Web Docs"
[4]: https://datatracker.ietf.org/doc/html/rfc7230?utm_source=chatgpt.com "RFC 7230 - Hypertext Transfer Protocol (HTTP/1.1)"
[5]: https://datatracker.ietf.org/doc/html/rfc7231?utm_source=chatgpt.com "RFC 7231 - Hypertext Transfer Protocol (HTTP/1.1)"