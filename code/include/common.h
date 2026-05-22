/**
 * file for common variables to be coerent in the whole project
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define NAME_MAX           64
#define LABEL_MAX          32
#define VALUE_MAX          64
#define PATH_MAX           256
#define PAYLOAD_MAX        512
#define LINE_MAX           512

#define RUNTIME_DIR        "runtime"
#define FIFO_DIR           "runtime/fifos"
#define LOG_DIR            "runtime/logs"
#define PID_DIR            "runtime/pids"
#define REGISTRY_DIR       "runtime/registry"
#define REGISTRY_FILE      "runtime/registry/devices.registry"

#define CONTROLLER_ID      0
#define NO_PARENT         -1

#define MIN_RANDOM_DELAY_S 1
#define MAX_RANDOM_DELAY_S 3

typedef int32_t device_id ;

typedef enum {
    STATE_UNKNOWN = 0,
    STATE_OFF,
    STATE_ON,
    STATE_OPEN,
    STATE_CLOSED,
    STATE_MANUAL_OVERRIDE
} state ;

typedef enum {
    MODE_READ = 0,
    MODE_WRITE
} mode ;

#endif