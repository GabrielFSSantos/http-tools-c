// Envia GET e salva em arquivo OU retorna corpo em memória.

#pragma once
#include <stddef.h>

// http_get:
//  - Se save_path != NULL: grava o corpo no arquivo e retorna status HTTP.
//  - Se save_path == NULL: aloca e retorna o corpo em *out_body e tamanho em *out_len.
// Retorna o código de status HTTP (ex.: 200) ou negativo em erro de transporte.
int http_get(const char *full_url, const char *save_path, char **out_body, size_t *out_len);