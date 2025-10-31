#ifndef FS_H
#define FS_H
#include <stdbool.h>

bool fs_join_and_sanitize(const char *root, const char *url_path, char out_path[]);
void fs_serve_path(int fd, const char *url_path, const char *fs_path);
void fs_send_dir_json(int fd, const char *fs_dir);

#endif