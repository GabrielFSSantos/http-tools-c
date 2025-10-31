// Implementação simples de parser de lista JSON e montagem de URL ?list=1.

#include "json_list.h"
#include <string.h>
#include <stdio.h>

void build_list_url(const char *base_url, char *out, size_t outsz) {
    const char *q = strchr(base_url, '?');
    if (q) snprintf(out, outsz, "%s&list=1", base_url);
    else   snprintf(out, outsz, "%s?list=1", base_url);
}

// Parsing JSON MUITO simples: extrai strings entre aspas (["a","b",...])
size_t parse_json_list(const char *json, char items[][512], size_t max_items) {
    size_t count = 0;
    const char *p = json;
    while (*p && count < max_items) {
        const char *a = strchr(p, '\"');
        if (!a) break;
        a++;
        const char *b = strchr(a, '\"');
        if (!b) break;
        size_t n = (size_t)(b - a);
        if (n >= 512) n = 511;
        memcpy(items[count], a, n);
        items[count][n] = '\0';
        count++;
        p = b + 1;
    }
    return count;
}