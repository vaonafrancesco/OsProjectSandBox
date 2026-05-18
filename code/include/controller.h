#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <sys/types.h>

#include "common.h"
#include "device.h"

#define CONTROLLER_MAX_DEVICES 128

typedef struct {
    device_id_t id;
    device_type_t type;
    pid_t pid;
    domo_state_t state;
    device_id_t parent_id;
    char fifo_path[DOMO_PATH_MAX];
    bool alive;
} controller_device_entry_t;

typedef struct {
    int running;
    device_id_t next_device_id;
    int device_count;
    controller_device_entry_t devices[CONTROLLER_MAX_DEVICES];
} controller_t;

int controller_init(controller_t *controller);
int controller_run(controller_t *controller);
int controller_destroy(controller_t *controller);

int controller_add_device(controller_t *controller, device_type_t type);
int controller_delete_device(controller_t *controller, device_id_t id);
int controller_list_devices(controller_t *controller);
int controller_info_device(controller_t *controller, device_id_t id);
int controller_switch_device(controller_t *controller, device_id_t id, const char *label, const char *pos);
int controller_link_devices(controller_t *controller, device_id_t child_id, device_id_t parent_id);

controller_device_entry_t *controller_find_device(controller_t *controller, device_id_t id);
const controller_device_entry_t *controller_find_device_const(const controller_t *controller, device_id_t id);

#endif