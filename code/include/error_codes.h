#ifndef ERROR_CODES_H
#define ERROR_CODES_H

typedef enum {
    OK = 0,
    ERR_DEVICE_NOT_FOUND = 1,
    ERR_INVALID_COMMAND = 2,
    ERR_IPC_FAILURE = 3,
    ERR_INVALID_PARAMETERS = 4,
    ERR_LINK_FAILED = 5,
    ERR_DEVICE_TYPE_MISMATCH = 6,
    ERR_ALREADY_LINKED = 7,
    ERR_SELF_LINK = 8,
    ERR_CYCLE_DETECTED = 9,
    ERR_NOT_ALLOWED = 10,
    ERR_CHILD_CRASHED = 11,
    ERR_TIMEOUT = 12,
    ERR_INVALID_STATE = 13,
    ERR_INVALID_TIME = 14,
    ERR_SYSTEM = 100
} error_code_t;

const char *error_str(int code);

#endif