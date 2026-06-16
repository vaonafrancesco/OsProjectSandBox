#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"


//ipc_send_request_and_wait
//sends a request message and waits for a reply on the provided fd_in.
// Uses select() to implement a timeout, preventing the process from blocking if the target device is unresponsive or dead.

int ipc_send_request_and_wait(const domo_message *request, domo_message *response, int fd_in)
{
    if (request == NULL || response == NULL || fd_in < 0) {
        return ERR_IPC_FAILURE;
    }

    int send_status = ipc_send_message(request);
    if (send_status != OK) {
        return send_status;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd_in, &read_fds);

    struct timeval tv;
    tv.tv_sec = TIMEOUT_DEVICE;
    tv.tv_usec = 0;

    int retval = select(fd_in + 1, &read_fds, NULL, NULL, &tv);
    if (retval == -1) {
        perror("select");
        return ERR_IPC_FAILURE;
    }

    if (retval == 0) {
        return ERR_IPC_FAILURE;
    }

    return ipc_recv_message(fd_in, response);
}