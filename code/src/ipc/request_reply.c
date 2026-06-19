#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

int ipc_send_request_and_wait(const domo_message *request, domo_message *response, int fd_in)
{
    fd_set read_fds;
    struct timeval tv;
    int send_status;
    int retval;

    if (request == NULL || response == NULL || fd_in < 0) {
        return ERR_IPC_FAILURE;
    }

    send_status = ipc_send_message(request);
    if (send_status != OK) {
        return send_status;
    }

    FD_ZERO(&read_fds);
    FD_SET(fd_in, &read_fds);

    tv.tv_sec = TIMEOUT_DEVICE;
    tv.tv_usec = 0;

    retval = select(fd_in + 1, &read_fds, NULL, NULL, &tv);
    if (retval < 0) {
        return ERR_IPC_FAILURE;
    }

    if (retval == 0) {
        return ERR_TIMEOUT;
    }

    return ipc_recv_message(fd_in, response);
}