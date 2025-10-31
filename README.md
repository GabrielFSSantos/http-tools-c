# http-tools-c

Implementação de um servidor HTTP/1.1 em C (apenas **GET**) e um **cliente** HTTP simples para listagem e download. O foco é entender redes + HTTP na prática, com código direto, comentado e sem dependências externas.

> **Stack:** C99 (gcc), sockets Berkeley, POSIX; sem libs de terceiros.

## ✨ Funcionalidades

**Servidor HTTP**

* Atende **GET** a arquivos e diretórios dentro da **raiz** (document root).
* Determina **Content-Type** por extensão (html, css, js, png, jpg, svg, pdf, txt…).
* **Diretórios:**

  * Se existir `index.html`, ele é **servido**.
  * Se **não** existir `index.html`, o servidor gera **listagem HTML**.
  * API de listagem: `/?list=1` retorna **JSON** com os nomes dos arquivos do diretório **excluindo** `index.html`.
* Respostas: `200 OK`, `404 Not Found`, `400 Bad Request` (parsing inválido) e `405 Method Not Allowed` (método ≠ GET).
* Higiene de caminho: normaliza URL, **recusa `..`** e ancora sob a raiz resolvida.

**Cliente HTTP**

* Baixa um único recurso: `./client http://host[:porta]/caminho` → salva em `./downloads/<arquivo>`.
* Lista itens de um diretório: `./client --list http://host:porta/dir/` (usa `/?list=1`).
* Baixa todos os itens listados: `./client --all http://host:porta/dir/` (usa `/?list=1`).
* Suporta corpo com **`Content-Length`** e **`Transfer-Encoding: chunked`** (decodificação implementada). ([RFC Editor][1])
* **URLs com espaços/acentos:** o cliente faz *URL-encoding por segmento de path* automaticamente (conforme “unreserved” da RFC 3986). ([MDN Web Docs][2])

---

## 🗂️ Estrutura

```
.
├─ server.c                          # main do servidor (args e http_run)
├─ client.c                          # main do cliente (roteia modos SINGLE/LIST/ALL)
│
├─ server_files/
│  ├─ http.c  http.h                 # socket, bind/listen/accept, parsing da 1ª linha, roteamento
│  ├─ fs.c    fs.h                   # join seguro raiz+URL, envio de arquivo e listagem/JSON (?list=1)
│  └─ util.c  util.h                 # MIME types, URL-decode, cabeçalhos e respostas 400/404/405
│
├─ client_files/
│  ├─ net.c     net.h                # getaddrinfo/socket/connect (IPv4/IPv6)
│  ├─ httpc.c   httpc.h              # HTTP GET (status/headers/chunked/mem vs arquivo)
│  ├─ url.c     url.h                # parse de URL, URL-encode por segmentos, utilidades
│  └─ io.c      io.h                 # helpers de I/O (linhas, trims) e pasta de downloads
│
├─ files/                            # “document root” padrão do servidor (coloque seus arquivos aqui)
├─ downloads/                        # saída padrão de downloads do cliente
├─ Makefile                          # alvos: server (padrão), client, run, clean
└─ README.md
```

> O cliente continua disponível como **binário único**, mas o código está dividido em `client_files/` para ficar mais didático.

---

## ✅ Pré-requisitos

* Linux/WSL com `gcc` e `make`.
* Porta livre (padrão **5050**).

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

Coloque o que deseja servir em `./files` (ou passe outra raiz).

```bash
mkdir -p ./files
echo "olá" > ./files/arquivo.txt

# raiz padrão ./files, porta 5050
./server

# ou escolhendo raiz e porta explicitamente
./server ./files 5050
```

Acesse no navegador: `http://localhost:5050/`

---

## ⬇️ Usar o cliente

### 1) Baixar um único recurso

```bash
# página inicial (salva como downloads/index.html)
./client http://localhost:5050/

# arquivo específico
./client http://localhost:5050/arquivo.txt
```

**URLs com espaço/acentos** – basta passar “do seu jeito”:
O cliente codifica cada **segmento do path** automaticamente:

```bash
# nomes com espaço:
./client "http://localhost:5050/Captura de tela 2025-03-16 114244.png"

# nomes com acentos:
./client "http://localhost:5050/relatório final.pdf"
```

### 2) Listar itens de um diretório

```bash
./client --list http://localhost:5050/
# saída: nomes de arquivos, sem o index.html
```

### 3) Baixar todos os itens listados

```bash
./client --all  http://localhost:5050/
# baixa todos os arquivos retornados por /?list=1 para ./downloads/
```

Verifique o resultado:

```bash
ls -lh downloads/
```

---

## 🔒 Notas de segurança

* O servidor **limpa/normaliza** o caminho e **recusa `..`** (Directory Traversal), juntando com a raiz obtida por `realpath`.
* Apenas **GET** é aceito; outros métodos recebem `405`.
* Sem TLS/HTTPS, auth, compactação, cache, HTTP/2 etc.

---

## 📚 Referências

* **HTTP/1.1 (mensagens e transporte)** — RFC 9112 (atualiza 7230/7231/7232/7233/7234): gramática das mensagens, *chunked*, etc.
* **HTTP Semantics** — RFC 9110: métodos, códigos de status e semântica geral.
* **Transfer-Encoding / chunked** — documentação MDN (conceitos e comportamento). ([RFC Editor][1])
* **URI – *unreserved characters*** (URL-encoding seguro por segmento) — RFC 3986. ([MDN Web Docs][2])

---

[1]: https://www.rfc-editor.org/rfc/rfc4395.txt?utm_source=chatgpt.com "RFC 4395"
[2]: https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Trailer?utm_source=chatgpt.com "Trailer header - HTTP - MDN Web Docs"