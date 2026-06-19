#include "error_codes.h"

const char *error_str(int code) {
    switch (code) {
        case OK: return "OK";
        case ERR_DEVICE_NOT_FOUND: return "Device not found. Check the name and try again.";
        case ERR_INVALID_COMMAND: return "This command is not supported.";
        case ERR_IPC_FAILURE: return "Communication failed. Please try again.";
        case ERR_INVALID_PARAMETERS: return "The parameters are not valid try again.";
        case ERR_LINK_FAILED: return "Could not create the link. Please try again.";
        case ERR_DEVICE_TYPE_MISMATCH: return "The selected devices are not compatible.";
        case ERR_ALREADY_LINKED: return "These devices are already linked.";
        case ERR_SELF_LINK: return "Self link not allowed. A device cannot be linked to itself.";
        case ERR_CYCLE_DETECTED: return "Cycle detected. Linking these devices would create an invalid loop.";
        case ERR_NOT_ALLOWED: return "Operation not allowed. The action is forbidden in the current state.";
        case ERR_CHILD_CRASHED: return "Child process crashed, restart the device and try again.";
        case ERR_TIMEOUT: return "The operation has timed out, the request took too long to complete.";
        case ERR_INVALID_STATE: return "The device is not in the correct state for this action.";
        case ERR_INVALID_TIME: return "Invalid time value. Use a valid time format and try again.";
        case ERR_PERMISSION_DENIED: return "Permission denied. This action requires manual interaction.";
        default: return "System error. Check logs or retry the operation.";
    }
}