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

static int ensure_runtime_dirs(void) {
    if (mkdir(DOMO_RUNTIME_DIR, 0777) != 0 && errno != EEXIST) return DOMO_ERR_SYSTEM;
    if (mkdir(DOMO_FIFO_DIR, 0777) != 0 && errno != EEXIST) return DOMO_ERR_SYSTEM;
    if (mkdir(DOMO_LOG_DIR, 0777) != 0 && errno != EEXIST) return DOMO_ERR_SYSTEM;
    if (mkdir(DOMO_PID_DIR, 0777) != 0 && errno != EEXIST) return DOMO_ERR_SYSTEM;
    if (mkdir(DOMO_REGISTRY_DIR, 0777) != 0 && errno != EEXIST) return DOMO_ERR_SYSTEM;
    return DOMO_OK;
}

static int write_registry(const controller_t *controller) {
    FILE *fp;
    int i;

    fp = fopen(DOMO_REGISTRY_FILE, "w");
    if (fp == NULL) {
        return DOMO_ERR_SYSTEM;
    }

    for (i = 0; i < controller->device_count; ++i) {
        const controller_device_entry_t *dev = &controller->devices[i];
        if (!dev->alive) {
            continue;
        }
        fprintf(fp, "%d %d %s\n", dev->id, (int)dev->pid, dev->fifo_path);
    }

    fclose(fp);
    return DOMO_OK;
}

static int spawn_bulb_process(device_id_t id, char *fifo_path, size_t fifo_path_len, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);
    if (domo_make_device_fifo_path(id, fifo_path, fifo_path_len) != DOMO_OK) {
        return DOMO_ERR_SYSTEM;
    }

    pid = fork();
    if (pid < 0) {
        return DOMO_ERR_SYSTEM;
    }

    if (pid == 0) {
        execl("./bin/domotics_controller",
              "./bin/domotics_controller",
              "--device-bulb",
              id_arg,
              (char *)NULL);
        _exit(DOMO_ERR_SYSTEM);
    }

    *pid_out = pid;
    return DOMO_OK;
}

controller_device_entry_t *controller_find_device(controller_t *controller, device_id_t id) {
    int i;

    if (controller == NULL) {
        return NULL;
    }

    for (i = 0; i < controller->device_count; ++i) {
        if (controller->devices[i].alive && controller->devices[i].id == id) {
            return &controller->devices[i];
        }
    }

    return NULL;
}

const controller_device_entry_t *controller_find_device_const(const controller_t *controller, device_id_t id) {
    int i;

    if (controller == NULL) {
        return NULL;
    }

    for (i = 0; i < controller->device_count; ++i) {
        if (controller->devices[i].alive && controller->devices[i].id == id) {
            return &controller->devices[i];
        }
    }

    return NULL;
}

int controller_init(controller_t *controller) {
    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    memset(controller, 0, sizeof(*controller));
    controller->running = 1;
    controller->next_device_id = 1;

    return ensure_runtime_dirs();
}

int controller_add_device(controller_t *controller, device_type_t type) {
    controller_device_entry_t *entry;
    pid_t pid;
    int rc;

    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    if (controller->device_count >= CONTROLLER_MAX_DEVICES) {
        return DOMO_ERR_NOT_ALLOWED;
    }

    if (type != DEVICE_BULB) {
        return DOMO_ERR_DEVICE_TYPE_MISMATCH;
    }

    entry = &controller->devices[controller->device_count];
    memset(entry, 0, sizeof(*entry));

    entry->id = controller->next_device_id++;
    entry->type = type;
    entry->state = DOMO_STATE_OFF;
    entry->parent_id = DOMO_CONTROLLER_ID;
    entry->alive = true;

    rc = spawn_bulb_process(entry->id, entry->fifo_path, sizeof(entry->fifo_path), &pid);
    if (rc != DOMO_OK) {
        return rc;
    }

    entry->pid = pid;
    controller->device_count++;

    rc = write_registry(controller);
    if (rc != DOMO_OK) {
        return rc;
    }

    printf("Added device: id=%d type=%s pid=%d\n",
           entry->id, device_type_str(entry->type), (int)entry->pid);

    return DOMO_OK;
}

int controller_delete_device(controller_t *controller, device_id_t id) {
    controller_device_entry_t *dev;
    int status;

    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device(controller, id);
    if (dev == NULL) {
        return DOMO_ERR_DEVICE_NOT_FOUND;
    }

    if (kill(dev->pid, SIGTERM) != 0) {
        return DOMO_ERR_SYSTEM;
    }

    waitpid(dev->pid, &status, 0);
    dev->alive = false;
    dev->pid = 0;

    return write_registry(controller);
}

int controller_list_devices(controller_t *controller) {
    int i;

    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    printf("ID\tTYPE\tPID\tSTATE\tPARENT\n");
    for (i = 0; i < controller->device_count; ++i) {
        controller_device_entry_t *dev = &controller->devices[i];
        if (!dev->alive) {
            continue;
        }

        printf("%d\t%s\t%d\t%d\t%d\n",
               dev->id,
               device_type_str(dev->type),
               (int)dev->pid,
               (int)dev->state,
               dev->parent_id);
    }

    return DOMO_OK;
}

int controller_info_device(controller_t *controller, device_id_t id) {
    const controller_device_entry_t *dev;
    domo_message_t req;
    domo_message_t resp;
    char reply_fifo[DOMO_PATH_MAX];
    int rc;

    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return DOMO_ERR_DEVICE_NOT_FOUND;
    }

    memset(&req, 0, sizeof(req));
    req.kind = DOMO_MSG_REQUEST;
    req.cmd = DOMO_CMD_INFO;
    req.src_id = DOMO_CONTROLLER_ID;
    req.dst_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();

    rc = domo_make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != DOMO_OK) {
        return rc;
    }

    rc = domo_request_reply(dev->fifo_path, reply_fifo, &req, &resp);
    if (rc != DOMO_OK) {
        return rc;
    }

    if (resp.status != DOMO_OK) {
        return resp.status;
    }

    printf("%s\n", resp.payload);
    return DOMO_OK;
}

int controller_switch_device(controller_t *controller, device_id_t id, const char *label, const char *pos) {
    const controller_device_entry_t *dev;
    domo_message_t req;
    domo_message_t resp;
    char reply_fifo[DOMO_PATH_MAX];
    int rc;

    if (controller == NULL || label == NULL || pos == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return DOMO_ERR_DEVICE_NOT_FOUND;
    }

    memset(&req, 0, sizeof(req));
    req.kind = DOMO_MSG_REQUEST;
    req.cmd = DOMO_CMD_SWITCH;
    req.src_id = DOMO_CONTROLLER_ID;
    req.dst_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.arg1, sizeof(req.arg1), "%s", label);
    snprintf(req.arg2, sizeof(req.arg2), "%s", pos);

    rc = domo_make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != DOMO_OK) {
        return rc;
    }

    rc = domo_request_reply(dev->fifo_path, reply_fifo, &req, &resp);
    if (rc != DOMO_OK) {
        return rc;
    }

    if (resp.status != DOMO_OK) {
        return resp.status;
    }

    printf("%s\n", resp.payload[0] ? resp.payload : "switch ok");
    return DOMO_OK;
}

int controller_link_devices(controller_t *controller, device_id_t child_id, device_id_t parent_id) {
    (void)controller;
    (void)child_id;
    (void)parent_id;
    return DOMO_ERR_NOT_ALLOWED;
}

int controller_destroy(controller_t *controller) {
    int i;

    if (controller == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < controller->device_count; ++i) {
        controller_device_entry_t *dev = &controller->devices[i];
        if (dev->alive) {
            kill(dev->pid, SIGTERM);
            waitpid(dev->pid, NULL, 0);
            dev->alive = false;
        }
    }

    write_registry(controller);
    return DOMO_OK;
}