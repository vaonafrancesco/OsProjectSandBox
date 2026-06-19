#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

int ipc_open_fifo_read(int my_id, int *keepalive_fd)
{
    char fifo_path[256];
    int fd_in;
    int fd_keep = -1;

    snprintf(fifo_path, sizeof(fifo_path), "%s%d", FIFO_PATH_PREFIX, my_id);

    if (mkfifo(fifo_path, 0666) == -1 && errno != EEXIST) {
        return ERR_IPC_FAILURE;
    }

    fd_in = open(fifo_path, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0) {
        return ERR_IPC_FAILURE;
    }

    fd_keep = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_keep < 0) {
        close(fd_in);
        if (keepalive_fd != NULL) {
            *keepalive_fd = -1;
        }
        return ERR_IPC_FAILURE;
    }

    if (keepalive_fd != NULL) {
        *keepalive_fd = fd_keep;
    } else {
        close(fd_keep);
    }

    return fd_in;
}