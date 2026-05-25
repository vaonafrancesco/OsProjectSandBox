#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "controller.h"
#include "error_codes.h"
#include "ipc.h"
#include "routing.h"

static int ensure_runtime_dirs(void) {
    if(mkdir(RUNTIME_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(FIFO_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(LOG_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(PID_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(REGISTRY_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    return OK;
}

static int write_registry(const controller *controller) 
{
    FILE *fp;
    int i;

    fp = fopen(REGISTRY_FILE, "w");
    if(fp == NULL) {
        return ERR_SYSTEM;
    }

    for(i = 0; i < controller->device_count; ++i) {
        const device *dev = &controller->devices[i];
        if(dev->info.pid == 0) {
            continue;
        }
        fprintf(fp, "%d %d %s\n", dev->info.id, (int)dev->info.pid, dev->info.fifo_path);
    }

    fclose(fp);
    return OK;
}

static int spawn_bulb_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if(pid == 0) {
        // child process - exec bulb
        execl("./bin/domotics_controller",
              "./bin/domotics_controller",
              "--device-bulb",
              id_arg,
              (char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}

device *controller_find_device(controller *controller, device_id id) {
    int i;

    if(controller == NULL) {
        return NULL;
    }

    for(i = 0; i < controller->device_count; ++i) {
        if(controller->devices[i].info.pid != 0 && controller->devices[i].info.id == id) {
            return &controller->devices[i];
        }
    }

    return NULL;
}

const device *controller_find_device_const(const controller *controller, device_id id) 
{
    int i;

    if (controller == NULL) {
        return NULL;
    }

    for (i = 0; i < controller->device_count; ++i) {
        if (controller->devices[i].info.pid != 0 && controller->devices[i].info.id == id) {
            return &controller->devices[i];
        }
    }

    return NULL;
}

int controller_init(controller *controller) {
    if(controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(controller, 0, sizeof(*controller));
    controller->running = 1;
    controller->next_device_id = 1;

    return ensure_runtime_dirs();
}

int controller_add_device(controller *controller, device_type type) {
    device *dev;
    pid_t pid;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if(controller->device_count >= CONTROLLER_MAX_DEVICES) {
        return ERR_NOT_ALLOWED;
    }

    if (type != DEVICE_BULB) {
        return ERR_DEVICE_TYPE_MISMATCH;
        printf("Device not recognized");
    }

    dev = &controller->devices[controller->device_count];
    memset(dev, 0, sizeof(*dev));

    rc = device_common_init(dev, controller->next_device_id++, type);
    if(rc != OK) {
        return rc;
    }

    dev->info.logical_parent_id = CONTROLLER_ID;

    rc = spawn_bulb_process(dev->info.id, &pid);
    if (rc != OK) {
        return rc;
    }

    dev->info.pid = pid;
    controller->device_count++;

    rc = write_registry(controller);
    if(rc != OK) {
        return rc;
    }

    printf("Added device: id=%d type=%s pid=%d\n",
           dev->info.id, device_type_str(dev->info.type), (int)dev->info.pid);

    return OK;
}

int controller_delete_device(controller *controller, device_id id) 
{
    device *dev;
    int status;

    if(controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device(controller, id);
    int tempID = dev->info.id;
    const char *tempDeviceType = device_type_str(dev->info.type);
    int tempPid = (int)dev->info.pid;

    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if(kill(dev->info.pid, SIGTERM) != 0) {
        return ERR_SYSTEM;
    }

    //aspettiamo che il processo finisca di chiudersi in sicurezza
    waitpid(dev->info.pid, &status, 0);
    dev->info.pid = 0;

    printf("Deleted device: id=%d type=%s pid=%d\n",
           tempID, tempDeviceType, (tempPid));

    return write_registry(controller);
}

int controller_list_devices(controller *controller) {
    int i;

    if(controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    printf("ID\tTYPE\tPID\tSTATE\tPARENT\n");
    for(i = 0; i < controller->device_count; ++i) {
        device *dev = &controller->devices[i];
        if(dev->info.pid == 0) {
            continue;
        }

        printf("%d\t%s\t%d\t%d\t%d\n",
               dev->info.id,
               device_type_str(dev->info.type),
               (int)dev->info.pid,
               (int)dev->info.state,
               dev->info.logical_parent_id);
    }

    return OK;
}

int controller_info_device(controller *controller, device_id id) {
    const device *dev;
    domo_message req;
    domo_message resp;
    char reply_fifo[PATH_MAX];
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if(dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_INFO);    
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();

    rc = make_reply_fifo_path(getpid(), req.request_id, 
                              reply_fifo, sizeof(reply_fifo));
    if(rc != OK) {
        return rc;
    }

    rc = request_reply(dev->info.fifo_path, reply_fifo, &req, &resp);
    if (rc != OK) {
        return rc;
    }

    if(resp.status != OK) {
        return resp.status;
    }

    printf("%s\n", resp.payload);
    return OK;
}

int controller_switch_device(controller *controller, device_id id, const char *label, const char *pos) 
{
    const device *dev;
    domo_message req;
    domo_message resp;
    char reply_fifo[PATH_MAX];
    int rc;

    if(controller == NULL || label == NULL || pos == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_SWITCH);
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.arg1, sizeof(req.arg1), "%s", label);
    snprintf(req.arg2, sizeof(req.arg2), "%s", pos);

    rc = make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo));
    if(rc != OK) {
        return rc;
    }

    rc = request_reply(dev->info.fifo_path, reply_fifo, &req, &resp);
    if(rc != OK) {
        return rc;
    }

    if (resp.status != OK) {
        return resp.status;
    }

    printf("%s\n", resp.payload[0] ? resp.payload : "switch ok");
    return OK;
}

int controller_link_devices(controller *controller, device_id child_id, device_id parent_id) {
    // TODO: implement device linking
    (void)controller;
    (void)child_id;
    (void)parent_id;
    return ERR_NOT_ALLOWED;
}

int controller_destroy(controller *controller) 
{
    int i;

    if(controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    // cleanup all devices
    for(i = 0; i < controller->device_count; ++i) {
        device *dev = &controller->devices[i];
        if (dev->info.pid != 0) {
            kill(dev->info.pid, SIGTERM);
            waitpid(dev->info.pid, NULL, 0);
            dev->info.pid = 0;
        }
    }

    write_registry(controller);
    return OK;
}