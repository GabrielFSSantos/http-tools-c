// server_files/util.h
#ifndef UTIL_H
#define UTIL_H
#include <stddef.h>

extern const char *g_root_dir;

const char *util_mime_type(const char *path);
void util_url_decode(char *s);
void util_send_headers(int fd, const char *status, const char *ctype, long content_length);
void util_send_404(int fd); 
void util_send_400(int fd);
void util_send_405(int fd);

#endif