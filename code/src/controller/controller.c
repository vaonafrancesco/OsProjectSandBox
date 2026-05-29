#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
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
#include "protocol.h"
#include "routing.h"

static int ensure_runtime_dirs(void) {
    if(mkdir(RUNTIME_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(FIFO_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(LOG_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(PID_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(REGISTRY_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    return OK;
}

//just to have it here so it's more clear 
static bool fifo_not_ready_errno(int err) {
    return err == ENXIO || err == ENOENT;
}

/**
 * method to wait for the fifo of the device to be ready so that there are no problem with race conditions
 */
static int wait_device_fifo_ready(const char *fifo_path) {
    int retries =TIMEOUT_DEVICE;

    while (retries-- > 0) {
        int ready_fd =open(fifo_path,O_WRONLY|O_NONBLOCK);
        if(ready_fd >=0) {
            close(ready_fd);
            return OK;}
        if(!fifo_not_ready_errno(errno)) {
            return ERR_SYSTEM;
        }
        sleep(1) ;
    }

    return ERR_TIMEOUT ;
}

/**
 * 
 */
static void controller_add_rollback(device_id id, pid_t pid, int routing_added) {
    char fifo_path[PATH_MAX];

    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
    if (routing_added) {
        routing_remove_node(id);
    }
    if (make_device_fifo_path(id, fifo_path, sizeof(fifo_path)) == OK) {
        unlink(fifo_path);
    }
}

static int write_registry(const controller *controller)
{
    FILE *fp;
    int i;
    int rc;

    rc = ensure_runtime_dirs();
    if (rc != OK) {
        return rc;
    }

    fp = fopen(REGISTRY_FILE, "w");
    if (fp == NULL) {
        return ERR_SYSTEM;
    }

    for (i = 0; i < controller->device_count; ++i) {
        const device *dev = &controller->devices[i];
        if (dev->info.pid == 0) {
            continue;
        }
        if (fprintf(fp, "%d %d %s\n",
                    dev->info.id,
                    (int)dev->info.pid,
                    dev->info.fifo_path) < 0) {
            fclose(fp);
            return ERR_SYSTEM;
        }
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        return ERR_SYSTEM;
    }

    fclose(fp);
    return OK;
}

static int controller_send_del_message(const device *dev) {
    domo_message req;

    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID);
    snprintf(req.command, sizeof(req.command), "%s", CMD_DEL);
    req.target_id = dev->info.id;
    req.src_id = CONTROLLER_ID;
    req.dst_id = dev->info.id;
    req.src_pid = getpid();
    req.request_id = dev->info.id;

    return send_message_to_fifo(dev->info.fifo_path, &req);
}

static int controller_wait_device_exit(pid_t pid) {
    int status;
    int waited = 0;

    while (waited < TIMEOUT_DEVICE) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            return OK;
        }
        if (w < 0 && errno == ECHILD) {
            return OK;
        }
        sleep(1);
        waited++;
    }

    kill(pid, SIGTERM);
    for (int i = 0; i < 3; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid) {
            return OK;
        }
        sleep(1);
    }

    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return OK;
}

static int controller_notify_parent_child_removed(const controller *controller,
                                                  device_id child_id,
                                                  int parent_id) {
    const device *parent;
    domo_message msg;
    int rc;

    if (parent_id == CONTROLLER_ID) {
        return OK;
    }

    parent = controller_find_device_const(controller, parent_id);
    if (parent == NULL) {
        return OK;
    }

    memset(&msg, 0, sizeof(msg));
    snprintf(msg.sender_id, sizeof(msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(msg.command, sizeof(msg.command), "%s", CMD_STATUS);
    msg.target_id = parent_id;
    msg.src_id = CONTROLLER_ID;
    msg.dst_id = parent_id;
    msg.src_pid = getpid();
    msg.request_id = child_id;
    snprintf(msg.payload, sizeof(msg.payload), "child_removed,%d", child_id);

    rc = send_message_to_fifo(parent->info.fifo_path, &msg);
    if (rc == ERR_DEVICE_NOT_FOUND || rc == ERR_IPC_FAILURE) {
        return OK;
    }
    return rc;
}

static int controller_delete_children_cascade(controller *controller, device_id parent_id) {
    device_id children[CONTROLLER_MAX_DEVICES];
    int count = 0;
    int rc;
    int i;

    rc = routing_collect_children(parent_id, children, CONTROLLER_MAX_DEVICES, &count);
    if (rc != OK) {
        return rc;
    }

    for (i = 0; i < count; i++) {
        rc = controller_delete_device(controller, children[i]);
        if (rc != OK) {
            return rc;
        }
    }

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
        // child process - exec bulb --modifica
        execl("./bin/domotics_controller",
              "controller",
              "--device-bulb",
              id_arg,
              (char *)NULL);
        perror("execl failed");
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}


static int spawn_window_process(device_id id, pid_t *pid_out) {
    pid_t pid ;
    char id_arg[32] ;

    snprintf(id_arg, sizeof(id_arg),"%d" ,id) ;

    pid = fork();
    if (pid<0)
    {
        return ERR_SYSTEM;}

    if(pid == 0){ //--modifica
        execl("./bin/domotics_controller","controller","--device-window",id_arg,(char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out=pid;
    return OK ;
}

static int spawn_fridge_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg,sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if(pid == 0){
        // child process - exec fridge  --modifica
        execl("./bin/domotics_controller","controller","--device-fridge",id_arg,(char *)NULL);
        _exit(ERR_SYSTEM);
    }
    *pid_out= pid;


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

    routing_init();

    return ensure_runtime_dirs();
}

int controller_add_device(controller *controller, device_type type) {
    device *dev;
    device_id id;
    pid_t pid = 0;
    int routing_added = 0;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (controller->device_count >= CONTROLLER_MAX_DEVICES) {
        return ERR_NOT_ALLOWED;
    }

    dev = &controller->devices[controller->device_count];
    memset(dev, 0, sizeof(*dev));

    id = controller->next_device_id++;
    rc = device_common_init(dev, id, type);
    if (rc != OK) {
        return rc;
    }

    dev->info.logical_parent_id = CONTROLLER_ID;

    switch (type) {
        case DEVICE_BULB:
            rc = spawn_bulb_process(dev->info.id, &pid);
            break;
        case DEVICE_WINDOW:
            rc = spawn_window_process(dev->info.id, &pid);
            break;
        case DEVICE_FRIDGE:
            rc = spawn_fridge_process(dev->info.id, &pid);
            break;
        default:
            return ERR_DEVICE_TYPE_MISMATCH;
    }

    if (rc != OK) {
        return rc;
    }

    dev->info.pid = pid;

    rc = wait_device_fifo_ready(dev->info.fifo_path);
    if (rc != OK) {
        controller_add_rollback(id, pid, routing_added);
        memset(dev, 0, sizeof(*dev));
        dev->info.pid = 0;
        return rc;
    }

    rc = routing_add_node(id, type);
    if (rc != OK) {
        controller_add_rollback(id, pid, routing_added);
        memset(dev, 0, sizeof(*dev));
        dev->info.pid = 0;
        return rc;
    }
    routing_added = 1;

    controller->device_count++;

    rc = write_registry(controller);
    if (rc != OK) {
        controller->device_count--;
        controller_add_rollback(id, pid, routing_added);
        memset(dev, 0, sizeof(*dev));
        return rc;
    }

    printf("Added device: id=%d type=%s pid=%d\n",
           dev->info.id, device_type_str(dev->info.type), (int)dev->info.pid);

    return OK;
}

int controller_delete_device(controller *controller, device_id id)
{
    device *dev;
    pid_t pid;
    int parent_id = CONTROLLER_ID;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device(controller, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if (is_control_device(dev->info.type)) {
        rc = controller_delete_children_cascade(controller, id);
        if (rc != OK) {
            return rc;
        }
    }

    routing_get_parent_id(id, &parent_id);

    pid = dev->info.pid;
    rc = controller_send_del_message(dev);
    if (rc != OK && rc != ERR_DEVICE_NOT_FOUND) {
        kill(pid, SIGTERM);
    }

    controller_wait_device_exit(pid);

    unlink(dev->info.fifo_path);

    routing_remove_node(id);
    controller_notify_parent_child_removed(controller, id, parent_id);

    dev->info.pid = 0;
    dev->info.logical_parent_id = NO_PARENT;

    rc = write_registry(controller);
    if (rc != OK) {
        return rc;
    }

    printf("Deleted device: id=%d\n", id);
    return OK;
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
    req.target_id = id; // FIX: Imposta anche target_id per coerenza
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID); //--modifica (aggiunto)
    snprintf(req.payload, sizeof(req.payload), "ALL"); //--modifica (aggiunto)

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
    req.target_id = id; // FIX: Imposta anche target_id per coerenza
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.arg1, sizeof(req.arg1), "%s", label);
    snprintf(req.arg2, sizeof(req.arg2), "%s", pos);
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID); //--modifica (aggiunto)
    snprintf(req.payload, sizeof(req.payload), "%s,%s", label, pos); //--modifica (aggiunto)

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

    for (i = 0; i < controller->device_count; ++i) {
        device *dev = &controller->devices[i];
        if (dev->info.pid != 0) {
            (void)controller_delete_device(controller, dev->info.id);
        }
    }

    write_registry(controller);
    return OK;
}
