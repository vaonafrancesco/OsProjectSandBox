#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"
#include "../../include/serialization.h"

#define DOMO_MSG_FIELD_COUNT 11

static int parse_int_field(const char *s, int *out)
{
    char *endptr;
    long value;

    if (s == NULL || out == NULL || *s == '\0') {
        return ERR_IPC_FAILURE;
    }

    errno = 0;
    value = strtol(s, &endptr, 10);

    if (errno != 0 || endptr == s || *endptr != '\0') {
        return ERR_IPC_FAILURE;
    }

    if (value < INT_MIN || value > INT_MAX) {
        return ERR_IPC_FAILURE;
    }

    *out = (int)value;
    return OK;
}

static int parse_pid_field(const char *s, pid_t *out)
{
    int temp;
    int rc;

    if (s == NULL || out == NULL) {
        return ERR_IPC_FAILURE;
    }

    rc = parse_int_field(s, &temp);
    if (rc != OK) {
        return rc;
    }

    *out = (pid_t)temp;
    return OK;
}

int serialize_message(const domo_message *msg, char *buffer, size_t max_len)
{
    int len;

    if (msg == NULL || buffer == NULL || max_len == 0) {
        return ERR_IPC_FAILURE;
    }

    len = snprintf(buffer, max_len,
                   "%s|%s|%d|%d|%d|%d|%d|%s|%s|%d|%s\n",
                   msg->sender_id,
                   msg->command,
                   msg->target_id,
                   msg->src_id,
                   msg->dst_id,
                   (int)msg->src_pid,
                   msg->request_id,
                   msg->arg1,
                   msg->arg2,
                   msg->status,
                   msg->payload);

    if (len < 0 || (size_t)len >= max_len) {
        return ERR_IPC_FAILURE;
    }

    return OK;
}

int deserialize_message(char *buffer, domo_message *msg)
{
    char *fields[DOMO_MSG_FIELD_COUNT];
    char *iter;
    int i;
    int rc;

    if (buffer == NULL || msg == NULL) {
        return ERR_IPC_FAILURE;
    }

    memset(msg, 0, sizeof(*msg));

    buffer[strcspn(buffer, "\n")] = '\0';
    iter = buffer;

    for (i = 0; i < DOMO_MSG_FIELD_COUNT - 1; ++i) {
        char *delim = strchr(iter, MSG_DELIMITER_CHAR);
        if (delim == NULL) {
            return ERR_IPC_FAILURE;
        }

        fields[i] = iter;
        *delim = '\0';
        iter = delim + 1;
    }

    fields[DOMO_MSG_FIELD_COUNT - 1] = iter;

    strncpy(msg->sender_id, fields[0], sizeof(msg->sender_id) - 1);
    msg->sender_id[sizeof(msg->sender_id) - 1] = '\0';

    strncpy(msg->command, fields[1], sizeof(msg->command) - 1);
    msg->command[sizeof(msg->command) - 1] = '\0';

    rc = parse_int_field(fields[2], &msg->target_id);
    if (rc != OK) {
        return rc;
    }

    if (strcmp(fields[0], EXT_SENDER_ID) == 0) {
        msg->src_id = -1;
    } else {
        rc = parse_int_field(fields[3], &msg->src_id);
        if (rc != OK) {
            return rc;
        }
    }

    rc = parse_int_field(fields[4], &msg->dst_id);
    if (rc != OK) {
        return rc;
    }

    rc = parse_pid_field(fields[5], &msg->src_pid);
    if (rc != OK) {
        return rc;
    }

    rc = parse_int_field(fields[6], &msg->request_id);
    if (rc != OK) {
        return rc;
    }

    strncpy(msg->arg1, fields[7], sizeof(msg->arg1) - 1);
    msg->arg1[sizeof(msg->arg1) - 1] = '\0';

    strncpy(msg->arg2, fields[8], sizeof(msg->arg2) - 1);
    msg->arg2[sizeof(msg->arg2) - 1] = '\0';

    rc = parse_int_field(fields[9], &msg->status);
    if (rc != OK) {
        return rc;
    }

    strncpy(msg->payload, fields[10], sizeof(msg->payload) - 1);
    msg->payload[sizeof(msg->payload) - 1] = '\0';

    msg->kind = MSG_REQUEST;

    return OK;
}