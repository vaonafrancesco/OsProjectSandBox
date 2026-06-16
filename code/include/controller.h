#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <sys/types.h>

#include "common.h"
#include "device.h"

#define CONTROLLER_MAX_DEVICES 128



typedef struct {
    int running ;
    device_id next_device_id;
    int device_count;
    device devices[CONTROLLER_MAX_DEVICES];
} controller ;

int controller_init(controller *controller);
int controller_run(controller *controller);
int controller_destroy(controller *controller) ;

int controller_add_device(controller *controller, device_type type);

int controller_delete_device(controller *controller, device_id id);

int controller_list_devices(controller *controller);



int controller_info_device(controller *controller, device_id id);
int controller_switch_device(controller *controller, device_id id,const char *label, const char *pos);
int controller_link_devices(controller *controller, device_id child_id, device_id parent_id);
int controller_finalize_dead_device(controller *ctrl, pid_t dead_pid, int status);

device *controller_find_device(controller *controller, device_id id);
const device *controller_find_device_const(const controller *controller, device_id id);

#endif