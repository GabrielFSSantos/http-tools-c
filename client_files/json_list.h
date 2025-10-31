// Suporte à listagem ?list=1: montar URL e fazer parse simples de JSON ["a","b",...]

#pragma once
#include <stddef.h>

void build_list_url(const char *base_url, char *out, size_t outsz);
size_t parse_json_list(const char *json, char items[][512], size_t max_items);