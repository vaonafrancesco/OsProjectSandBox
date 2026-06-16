#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

void ipc_create_message(domo_message *msg,
                        const char *sender,
                        const char *cmd,
                        int target,
                        const char *payload)
{
    if (msg == NULL) {
        return;
    }

    memset(msg, 0, sizeof(*msg));

    if (sender != NULL) {
        strncpy(msg->sender_id, sender, sizeof(msg->sender_id) - 1);
        msg->sender_id[sizeof(msg->sender_id) - 1] = '\0';
    } else {
        msg->sender_id[0] = '\0';
    }

    if (cmd != NULL) {
        strncpy(msg->command, cmd, sizeof(msg->command) - 1);
        msg->command[sizeof(msg->command) - 1] = '\0';
    } else {
        msg->command[0] = '\0';
    }

    msg->target_id = target;
    msg->src_id = -1;
    msg->dst_id = target;
    msg->src_pid = getpid();
    msg->request_id = 0;
    msg->status = OK;
    msg->kind = MSG_REQUEST;

    msg->arg1[0] = '\0';
    msg->arg2[0] = '\0';

    if (payload != NULL) {
        strncpy(msg->payload, payload, sizeof(msg->payload) - 1);
        msg->payload[sizeof(msg->payload) - 1] = '\0';
    } else {
        msg->payload[0] = '\0';
    }
}