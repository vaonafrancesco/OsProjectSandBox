#ifndef IPC_H
#define IPC_H

#include <stdbool.h>
#include <sys/types.h>
#include "protocol.h"

//message structure

typedef struct {
    char sender_id[16];         //String: can be a numeric ID (e.g., "0") or "EXT"
    char command[32];           //e.g., "SWITCH, "LINK, "INFO"
    int target_id;              //logical target device ID
    char payload[MAX_MSG_LEN];  // command-specific data (example: "power on")

    // Additional fields for request-reply pattern
    int kind;                   // MSG_REQUEST or MSG_RESPONSE
    int src_id;                 // source device ID
    int dst_id;                 // destination device ID
    pid_t src_pid;              // source process ID
    int request_id;             // unique request ID for reply matching
    char arg1[32];              // first argument (e.g., switch label)
    char arg2[32];              // second argument (e.g., switch position)
    int status;                 // response status (error code)
}domo_message;

// IPC and FIFO management (from fifo.c)
int ipc_open_fifo_read (int my_id, int *keepalive_fd);

// MEssage formatting and parsing (from message.c)
int ipc_recv_message(int fd_in, domo_message *msg);
int ipc_send_message (const domo_message *msg);

// Common IPC utilities (from ipc_common.c)
void ipc_print_message(const domo_message *msg);
void ipc_create_message(domo_message *msg, const char *sender, const char *cmd, int target, const char *payload);

// REquest reply pattern (from request_reply.c)

int ipc_send_request_and_wait(const domo_message *request, domo_message *response, int fd_in);

//serialization and deserialization (from serialization.c)

int serialize_message(const domo_message *msg, char *buffer, size_t max_len);
int deserialize_message(char *buffer, domo_message *msg);

#endif