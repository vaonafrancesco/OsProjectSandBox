#include "device.h"

const char *device_type_str(device_type type) {
    switch (type) {
        case DEVICE_CONTROLLER: return "controller";
        case DEVICE_HUB: return "hub";
        case DEVICE_TIMER: return "timer";
        case DEVICE_BULB: return "bulb";
        case DEVICE_WINDOW: return "window";
        case DEVICE_FRIDGE: return "fridge";
        default: return "unknown";
    }
}

bool device_is_control(device_type type) {
    return type == DEVICE_CONTROLLER || type == DEVICE_HUB || type == DEVICE_TIMER;
}

bool device_is_interaction(device_type type) {
    return type == DEVICE_BULB || type == DEVICE_WINDOW || type == DEVICE_FRIDGE;
}