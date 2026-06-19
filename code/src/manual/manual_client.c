#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "error_codes.h"
#include "ipc.h"

static int parse_device_id(const char *s, device_id *out_id) {
    char *end = NULL;
    long value;

    if (s == NULL || out_id == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *out_id = (device_id)value;
    return OK;
}

static int lookup_device_fifo(device_id id, char *fifo_path, size_t fifo_path_len) {
    FILE *fp;
    int file_id;
    int pid_value;
    char path[PATH_MAX];

    if (fifo_path == NULL || fifo_path_len == 0) {
        return ERR_INVALID_PARAMETERS;
    }

    fp = fopen(REGISTRY_FILE, "r");
    if (fp == NULL) {
        return ERR_IPC_FAILURE;
    }

    while (fscanf(fp, "%d %d %255s", &file_id, &pid_value, path) == 3) {
        if (file_id == id) {
            fclose(fp);
            snprintf(fifo_path, fifo_path_len, "%s", path);
            return OK;
        }
    }

    fclose(fp);
    return ERR_DEVICE_NOT_FOUND;
}

static int build_manual_request(device_id id,
                                const char *command,
                                const char *param1,
                                const char *param2,
                                domo_message *msg) {
    if (msg == NULL || command == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(msg, 0, sizeof(*msg));
    snprintf(msg->sender_id, sizeof(msg->sender_id), "%s", EXT_SENDER_ID);
    msg->target_id = id;
    msg->dst_id = id;

    if (strcmp(command, "info") == 0) {
        snprintf(msg->command, sizeof(msg->command), "%s", CMD_INFO);
        return OK;
    }

    if (strcmp(command, "switch") == 0) {
        if (param1 == NULL || param2 == NULL) {
            return ERR_INVALID_PARAMETERS;
        }

        
        snprintf(msg->command, sizeof(msg->command), "%s", CMD_SWITCH);
        snprintf(msg->arg1, sizeof(msg->arg1), "%s", param1);
        snprintf(msg->arg2, sizeof(msg->arg2), "%s", param2);
        return OK;
    }

    if (strcmp(command, "set") == 0 || strcmp(command, "SET") == 0) {
        if (param1 == NULL || param2 == NULL) {
            return ERR_INVALID_PARAMETERS;
        }

        
        snprintf(msg->command, sizeof(msg->command), "%s", "SET");
        snprintf(msg->arg1, sizeof(msg->arg1), "%s", param1);
        snprintf(msg->arg2, sizeof(msg->arg2), "%s", param2);
        return OK;
    }

    return ERR_INVALID_COMMAND;
}

int main(int argc, char **argv) {
    device_id id;
    domo_message request;
    char request_fifo[PATH_MAX];
    int rc;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <id> <command> [parameters]\n", argv[0]);
        return ERR_INVALID_PARAMETERS;
    }

    rc = parse_device_id(argv[1], &id);
    if (rc != OK) {
        fprintf(stderr, "The devide id is not valid.\n");
        return rc;
    }

    rc = lookup_device_fifo(id, request_fifo, sizeof(request_fifo));
    if (rc != OK) {
        fprintf(stderr, "the device lookup failed: %s\n", error_str(rc));
        return rc;
    }

    rc = build_manual_request(id,
                              argv[2],
                              argc > 3 ? argv[3] : NULL,
                              argc > 4 ? argv[4] : NULL,
                              &request);
    if (rc != OK) {
        fprintf(stderr, "Cannot build request: %s\n", error_str(rc));
        return rc;
    }

    rc = ipc_send_message(&request);
    if (rc != OK) {
        fprintf(stderr, "IPC send failed: %s\n", error_str(rc));
        return rc;
    }

    printf("Manual command sent successfully to device %d\n", id);

    return OK;
}