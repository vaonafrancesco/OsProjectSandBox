#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/error_codes.h"
#include "../../include/ipc.h"
#include "../../include/protocol.h"
#include "../../include/serialization.h"

int ipc_recv_message(int fd_in, domo_message *msg)
{
    char buffer[MAX_MSG_LEN];
    size_t total_read = 0;

    if (fd_in < 0 || msg == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(msg, 0, sizeof(*msg));
    memset(buffer, 0, sizeof(buffer));

    while (total_read < sizeof(buffer) - 1) {
        char ch;
        ssize_t bytes_read;

        do {
            bytes_read = read(fd_in, &ch, 1);
        } while (bytes_read < 0 && errno == EINTR);

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return (total_read == 0) ? ERR_TIMEOUT : ERR_IPC_FAILURE;
            }
            return ERR_IPC_FAILURE;
        }

        if (bytes_read == 0) {
            if (total_read == 0) {
                return ERR_TIMEOUT;
            }
            return ERR_IPC_FAILURE;
        }

        buffer[total_read++] = ch;

        if (ch == NEWLINE_CHAR) {
            buffer[total_read] = '\0';
            return deserialize_message(buffer, msg);
        }
    }

    return ERR_IPC_FAILURE;
}

int ipc_send_message(const domo_message *msg)
{
    char fifo_path[256];
    char buffer[MAX_MSG_LEN];
    int fd_out;
    int rc;
    ssize_t total_written;
    ssize_t len;

    if (msg == NULL) {
        return ERR_IPC_FAILURE;
    }

    if (snprintf(fifo_path, sizeof(fifo_path), "%s%d",
                 FIFO_PATH_PREFIX, msg->target_id) >= (int)sizeof(fifo_path)) {
        return ERR_IPC_FAILURE;
    }

    rc = serialize_message(msg, buffer, sizeof(buffer));
    if (rc != OK) {
        return rc;
    }

    fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_out < 0) {
        if (errno == ENXIO || errno == ENOENT) {
            return ERR_DEVICE_NOT_FOUND;
        }
        return ERR_IPC_FAILURE;
    }

    len = (ssize_t)strlen(buffer);
    total_written = 0;

    while (total_written < len) {
        ssize_t n;

        do {
            n = write(fd_out, buffer + total_written,
                      (size_t)(len - total_written));
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            close(fd_out);
            return ERR_IPC_FAILURE;
        }

        if (n == 0) {
            close(fd_out);
            return ERR_IPC_FAILURE;
        }

        total_written += n;
    }

    close(fd_out);
    return OK;
}