#ifndef DEVICE_H
#define DEVICE_H

#include "common.h"
#include "ipc.h"

typedef enum {
    DEVICE_CONTROLLER = 0,
    DEVICE_HUB,
    DEVICE_TIMER,
    DEVICE_BULB,
    DEVICE_WINDOW,
    DEVICE_FRIDGE
} device_type_t;

typedef struct {
    device_id_t id;
    device_type_t type;
    pid_t pid;
    device_id_t logical_parent_id;
    domo_state_t state;
    bool manual_override;
    char fifo_path[DOMO_PATH_MAX];
    char name[DOMO_NAME_MAX];
} device_info_t;

typedef struct device device_t;

typedef int (*device_init_fn)(device_t *dev);
typedef int (*device_handle_fn)(device_t *dev, const domo_message_t *req, domo_message_t *resp);
typedef int (*device_destroy_fn)(device_t *dev);

struct device {
    device_info_t info;

    int child_count;
    device_id_t child_ids[32];

    char registry_snapshot[DOMO_PAYLOAD_MAX];

    device_init_fn init;
    device_handle_fn handle_message;
    device_destroy_fn destroy;

    void *impl;
};

const char *device_type_str(device_type_t type);
bool device_is_control(device_type_t type);
bool device_is_interaction(device_type_t type);

int device_build_info_payload(const device_t *dev, char *buffer, size_t buffer_len);
int device_apply_switch(device_t *dev, const char *label, const char *position);
int device_set_parameter(device_t *dev, const char *key, const char *value);

#endif