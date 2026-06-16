#include "../../include/error_codes.h"

const char *error_str(int code)
{
    switch (code) {
        case OK: return "OK";
        case ERR_DEVICE_NOT_FOUND: return "Device not found";
        case ERR_INVALID_COMMAND: return "Invalid command";
        case ERR_IPC_FAILURE: return "IPC failure";
        case ERR_INVALID_PARAMETERS: return "Invalid parameters";
        case ERR_LINK_FAILED: return "Link failed";
        case ERR_DEVICE_TYPE_MISMATCH: return "Device type mismatch";
        case ERR_ALREADY_LINKED: return "Already linked";
        case ERR_SELF_LINK: return "Self link not allowed";
        case ERR_CYCLE_DETECTED: return "Cycle detected";
        case ERR_NOT_ALLOWED: return "Operation not allowed";
        case ERR_CHILD_CRASHED: return "Child device crashed";
        case ERR_TIMEOUT: return "Operation timed out";
        case ERR_INVALID_STATE: return "Invalid state";
        case ERR_INVALID_TIME: return "Invalid time";
        case ERR_SYSTEM: return "System error";
        default: return "Unknown error";
    }
}