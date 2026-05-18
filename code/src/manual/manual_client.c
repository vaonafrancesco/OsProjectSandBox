#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "error_codes.h"
#include "ipc.h"

static int parse_device_id(const char *s, device_id_t *out_id) {
    char *end = NULL;
    long value;

    if (s == NULL || out_id == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < 0) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    *out_id = (device_id_t)value;
    return DOMO_OK;
}

static int lookup_device_fifo(device_id_t id, char *fifo_path, size_t fifo_path_len) {
    FILE *fp;
    int file_id;
    int pid_value;
    char path[DOMO_PATH_MAX];

    if (fifo_path == NULL || fifo_path_len == 0) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    fp = fopen(DOMO_REGISTRY_FILE, "r");
    if (fp == NULL) {
        return DOMO_ERR_IPC_FAILURE;
    }

    while (fscanf(fp, "%d %d %255s", &file_id, &pid_value, path) == 3) {
        if (file_id == id) {
            fclose(fp);
            snprintf(fifo_path, fifo_path_len, "%s", path);
            return DOMO_OK;
        }
    }

    fclose(fp);
    return DOMO_ERR_DEVICE_NOT_FOUND;
}

static int build_manual_request(device_id_t id,
                                const char *command,
                                const char *param1,
                                const char *param2,
                                domo_message_t *msg) {
    if (msg == NULL || command == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    memset(msg, 0, sizeof(*msg));
    msg->kind = DOMO_MSG_REQUEST;
    msg->src_id = DOMO_CONTROLLER_ID;
    msg->dst_id = id;
    msg->src_pid = getpid();
    msg->request_id = (int)getpid();

    if (strcmp(command, "info") == 0) {
        msg->cmd = DOMO_CMD_INFO;
        return DOMO_OK;
    }

    if (strcmp(command, "switch") == 0) {
        if (param1 == NULL || param2 == NULL) {
            return DOMO_ERR_INVALID_PARAMETERS;
        }

        msg->cmd = DOMO_CMD_SWITCH;
        snprintf(msg->arg1, sizeof(msg->arg1), "%s", param1);
        snprintf(msg->arg2, sizeof(msg->arg2), "%s", param2);
        return DOMO_OK;
    }

    if (strcmp(command, "set") == 0) {
        if (param1 == NULL || param2 == NULL) {
            return DOMO_ERR_INVALID_PARAMETERS;
        }

        msg->cmd = DOMO_CMD_SET_PARAM;
        snprintf(msg->arg1, sizeof(msg->arg1), "%s", param1);
        snprintf(msg->arg2, sizeof(msg->arg2), "%s", param2);
        return DOMO_OK;
    }

    return DOMO_ERR_INVALID_COMMAND;
}

int main(int argc, char **argv) {
    device_id_t id;
    domo_message_t request;
    domo_message_t response;
    char request_fifo[DOMO_PATH_MAX];
    char reply_fifo[DOMO_PATH_MAX];
    int rc;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <id> <command> [parameters]\n", argv[0]);
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    rc = parse_device_id(argv[1], &id);
    if (rc != DOMO_OK) {
        fprintf(stderr, "Invalid device id.\n");
        return rc;
    }

    rc = lookup_device_fifo(id, request_fifo, sizeof(request_fifo));
    if (rc != DOMO_OK) {
        fprintf(stderr, "Device lookup failed: %s\n", domo_error_str(rc));
        return rc;
    }

    rc = build_manual_request(id,
                              argv[2],
                              argc > 3 ? argv[3] : NULL,
                              argc > 4 ? argv[4] : NULL,
                              &request);
    if (rc != DOMO_OK) {
        fprintf(stderr, "Cannot build request: %s\n", domo_error_str(rc));
        return rc;
    }

    rc = domo_make_reply_fifo_path(getpid(), request.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != DOMO_OK) {
        fprintf(stderr, "Cannot build reply fifo path: %s\n", domo_error_str(rc));
        return rc;
    }

    rc = domo_request_reply(request_fifo, reply_fifo, &request, &response);
    if (rc != DOMO_OK) {
        fprintf(stderr, "IPC request failed: %s\n", domo_error_str(rc));
        return rc;
    }

    if (response.status != DOMO_OK) {
        fprintf(stderr, "Device error: %s\n", domo_error_str(response.status));
        return response.status;
    }

    if (response.payload[0] != '\0') {
        printf("%s\n", response.payload);
    }

    return DOMO_OK;
}