#ifndef IPC_H
#define IPC_H

#include "common.h"
#include "error_codes.h"

typedef enum {
    DOMO_MSG_INVALID = 0,
    DOMO_MSG_REQUEST,
    DOMO_MSG_RESPONSE,
    DOMO_MSG_EVENT
} domo_msg_kind_t;

typedef enum {
    DOMO_CMD_NONE = 0,
    DOMO_CMD_PING,
    DOMO_CMD_INFO,
    DOMO_CMD_SWITCH,
    DOMO_CMD_SET_PARAM,
    DOMO_CMD_GET_STATE,
    DOMO_CMD_LINK_PARENT,
    DOMO_CMD_ADD_CHILD,
    DOMO_CMD_REMOVE_CHILD,
    DOMO_CMD_TERMINATE,
    DOMO_CMD_NOTIFY_OVERRIDE,
    DOMO_CMD_NOTIFY_CRASH
} domo_cmd_t;

typedef struct {
    domo_msg_kind_t kind;
    domo_cmd_t cmd;
    device_id_t src_id;
    device_id_t dst_id;
    pid_t src_pid;
    int request_id;
    int status;
    char arg1[DOMO_LABEL_MAX];
    char arg2[DOMO_VALUE_MAX];
    char payload[DOMO_PAYLOAD_MAX];
} domo_message_t;

int domo_make_device_fifo_path(device_id_t id, char *buffer, size_t buffer_len);
int domo_make_reply_fifo_path(pid_t pid, int request_id, char *buffer, size_t buffer_len);

int domo_send_message(const char *fifo_path, const domo_message_t *msg);
int domo_recv_message(int fd, domo_message_t *msg);
int domo_request_reply(const char *request_fifo,
                       const char *reply_fifo,
                       const domo_message_t *request,
                       domo_message_t *response);

#endif