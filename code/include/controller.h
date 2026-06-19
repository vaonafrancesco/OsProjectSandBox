#ifndef CONTROLLER_H
#define CONTROLLER_H

#include <sys/types.h>
#include <time.h>

#include "common.h"
#include "device.h"
#include "ipc.h"

#define CONTROLLER_MAX_DEVICES 128
#define CONTROLLER_MAX_PENDING 64

typedef enum {
    CTRL_REQ_NONE = 0,
    CTRL_REQ_INFO,
    CTRL_REQ_SWITCH
} controller_request_kind;

typedef struct {
    int in_use;
    int request_id;
    controller_request_kind kind;
    device_id target_id;
    device_type target_type;
    time_t deadline;
    int reply_fd;
    char reply_fifo_path[PATH_MAX];
    char extra1[64];
    char extra2[64];
} pending_request;

typedef struct {
    int running;
    device_id next_device_id;
    int device_count;
    device devices[CONTROLLER_MAX_DEVICES] ;
    pending_request pending[CONTROLLER_MAX_PENDING] ;
} controller;

int controller_init(controller *controller);
int controller_run(controller *controller);
int controller_destroy(controller *controller);

int controller_add_device(controller *controller, device_type type);
int controller_delete_device(controller *controller, device_id id);
int controller_list_devices(controller *controller);

int controller_info_device(controller *controller, device_id id);
int controller_switch_device(controller *controller, device_id id, const char *label, const char *pos);
int controller_link_devices(controller *controller, device_id child_id, device_id parent_id);
int controller_finalize_dead_device(controller *ctrl, pid_t dead_pid, int status);

int controller_complete_pending_fd(controller *ctrl, int reply_fd);
int controller_expire_pending(controller *ctrl);

device *controller_find_device(controller *controller, device_id id);
const device *controller_find_device_const(const controller *controller, device_id id);

#endif