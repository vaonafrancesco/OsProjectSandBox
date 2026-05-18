#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define DOMO_NAME_MAX           64
#define DOMO_LABEL_MAX          32
#define DOMO_VALUE_MAX          64
#define DOMO_PATH_MAX           256
#define DOMO_PAYLOAD_MAX        512
#define DOMO_LINE_MAX           512

#define DOMO_RUNTIME_DIR        "runtime"
#define DOMO_FIFO_DIR           "runtime/fifos"
#define DOMO_LOG_DIR            "runtime/logs"
#define DOMO_PID_DIR            "runtime/pids"
#define DOMO_REGISTRY_DIR       "runtime/registry"
#define DOMO_REGISTRY_FILE      "runtime/registry/devices.registry"

#define DOMO_CONTROLLER_ID      0
#define DOMO_NO_PARENT         -1

#define DOMO_MIN_RANDOM_DELAY_S 1
#define DOMO_MAX_RANDOM_DELAY_S 3

typedef int32_t device_id_t;

typedef enum {
    DOMO_STATE_UNKNOWN = 0,
    DOMO_STATE_OFF,
    DOMO_STATE_ON,
    DOMO_STATE_OPEN,
    DOMO_STATE_CLOSED,
    DOMO_STATE_MANUAL_OVERRIDE
} domo_state_t;

typedef enum {
    DOMO_MODE_READ = 0,
    DOMO_MODE_WRITE
} domo_mode_t;

#endif