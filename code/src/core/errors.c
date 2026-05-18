#include "error_codes.h"

const char *domo_error_str(int code) {
    switch (code) {
        case DOMO_OK: return "OK";
        case DOMO_ERR_DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
        case DOMO_ERR_INVALID_COMMAND: return "INVALID_COMMAND";
        case DOMO_ERR_IPC_FAILURE: return "IPC_FAILURE";
        case DOMO_ERR_INVALID_PARAMETERS: return "INVALID_PARAMETERS";
        case DOMO_ERR_LINK_FAILED: return "LINK_FAILED";
        case DOMO_ERR_DEVICE_TYPE_MISMATCH: return "DEVICE_TYPE_MISMATCH";
        case DOMO_ERR_ALREADY_LINKED: return "ALREADY_LINKED";
        case DOMO_ERR_SELF_LINK: return "SELF_LINK";
        case DOMO_ERR_CYCLE_DETECTED: return "CYCLE_DETECTED";
        case DOMO_ERR_NOT_ALLOWED: return "NOT_ALLOWED";
        case DOMO_ERR_CHILD_CRASHED: return "CHILD_CRASHED";
        case DOMO_ERR_TIMEOUT: return "TIMEOUT";
        case DOMO_ERR_INVALID_STATE: return "INVALID_STATE";
        case DOMO_ERR_INVALID_TIME: return "INVALID_TIME";
        default: return "SYSTEM_ERROR";
    }
}