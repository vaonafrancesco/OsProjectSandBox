#ifndef IPC_H
#define IPC_H

#include <stddef.h>
#include <sys/types.h>

#include "protocol.h"

typedef struct {
    char sender_id[16];
    char command[32];
    int target_id;

    int src_id;
    int dst_id;
    pid_t src_pid;
    int request_id;
    char arg1[32];
    char arg2[32];
    int status;
    char payload[MAX_MSG_LEN];

    int kind;   /* internal only: MSG_REQUEST or MSG_RESPONSE, not serialized */
} domo_message;

int ipc_open_fifo_read(int my_id, int *keepalive_fd);

int ipc_recv_message(int fd_in, domo_message *msg);
int ipc_send_message(const domo_message *msg);

void ipc_create_message(domo_message *msg,
                        const char *sender,
                        const char *cmd,
                        int target,
                        const char *payload);

int ipc_send_request_and_wait(const domo_message *request,
                              domo_message *response,
                              int fd_in);



#endif