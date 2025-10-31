// Assinaturas para parse de URL, encoding de segmentos e utilidades de path.

#pragma once
#include <stddef.h>
#include <stdbool.h>

bool parse_url(const char *url, char *host, size_t hostsz, int *port, char *path, size_t pathsz);
void url_encode_segment(const char *in, char *out, size_t outsz);
bool encode_path_of_url(const char *in_url, char *out_url, size_t outsz);
const char* pick_filename(const char *path);