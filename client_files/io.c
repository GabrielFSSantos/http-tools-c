// Implementação de helpers de I/O para o cliente.

#define _POSIX_C_SOURCE 200809L
#include "io.h"
#include "common.h"

#include <sys/stat.h>
#include <errno.h>

int ensure_download_dir(void) {
    if (mkdir(DOWNLOAD_DIR, 0777) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}