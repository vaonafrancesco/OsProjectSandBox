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
#include <stdarg.h>

#include "controller.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "routing.h"
#include "event_loop.h"
#include "device.h"

static int ensure_runtime_dirs(void) {
    if(mkdir(RUNTIME_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(FIFO_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(LOG_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(PID_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if(mkdir(REGISTRY_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    return OK;
}

static int controller_count_direct_children(const controller *controller,
                                            device_id id);
static int compute_control_timeout(int child_count);
static int compute_request_timeout(device_type type,
                                   int child_count);


static int controller_notify_parent_child_removed(const controller *controller,
                                                  device_id child_id,
                                                  int parent_id);
int write_registry(const controller *ctrl);
static int controller_find_device_index_by_pid(const controller *ctrl, pid_t pid);
static void controller_remove_device_fifo(const device *dev);                                  

static int fifo_not_ready_errno(int err)
{
    return err == ENOENT || err == ENXIO;
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
 * this method is to complete the operations that are necessary to do whena device is deleted
 */
static void controller_add_rollback(device_id id, pid_t pid, int routing_added) {
    char fifo_path[PATH_MAX];
    if (pid > 0) {
        kill(pid,SIGTERM);
        waitpid(pid,NULL, 0);}
    if(routing_added) {
        routing_remove_node(id) ;
    }
    if (make_device_fifo_path(id, fifo_path, sizeof(fifo_path)) == OK) {
        unlink(fifo_path) ;
    }
}

static int controller_find_device_index_by_id(const controller *controller, device_id id)
{
    int i;

    if (controller == NULL) {
        return -1;
    }

    for (i = 0; i < controller->device_count; ++i) {
        if (controller->devices[i].info.id == id) {
            return i;
        }
    }

    return -1;
}

static void controller_remove_device_at_index(controller *controller, int index)
{
    int i;

    if (controller == NULL) {
        return;
    }

    if (index < 0 || index >= controller->device_count) {
        return;
    }

    for (i = index; i < controller->device_count - 1; ++i) {
        controller->devices[i] = controller->devices[i + 1];
    }

    controller->device_count--;
}

static void controller_remove_device_fifo(const device *dev)
{
    if (dev == NULL) {
        return;
    }

    if (dev->info.fifo_path[0] != '\0') {
        unlink(dev->info.fifo_path);
    }
}

int controller_finalize_dead_device(controller *ctrl, pid_t dead_pid, int status)
{
    int index;
    device dead_device;
    device_id dead_id;
    int parent_id = CONTROLLER_ID;
    int rc;
    int notify_rc;


    if (ctrl == NULL || dead_pid <= 0) {
        return ERR_INVALID_PARAMETERS;
    }

    index = controller_find_device_index_by_pid(ctrl, dead_pid);
    if (index < 0) {
        return ERR_DEVICE_NOT_FOUND;
    }

    dead_device = ctrl->devices[index];
    dead_id = dead_device.info.id;

    rc = routing_get_parent_id(dead_id, &parent_id);
    if (rc != OK) {
        parent_id = CONTROLLER_ID;
    }

    if (WIFSIGNALED(status)) {
        fprintf(stderr,
                "\n[cleanup] Device crashed: id=%d pid=%ld signal=%d type=%s\n",
                dead_device.info.id,
                (long)dead_pid,
                WTERMSIG(status),
                device_type_str(dead_device.info.type));
    } else if (WIFEXITED(status)) {
        fprintf(stderr,
                "\n[cleanup] Device exited: id=%d pid=%ld exit_status=%d type=%s\n",
                dead_device.info.id,
                (long)dead_pid,
                WEXITSTATUS(status),
                device_type_str(dead_device.info.type));
    } else {
        fprintf(stderr,
                "\n[cleanup] Device terminated: id=%d pid=%ld type=%s\n",
                dead_device.info.id,
                (long)dead_pid,
                device_type_str(dead_device.info.type));
    }

    notify_rc = controller_notify_parent_child_removed(ctrl, dead_id, parent_id);
    if (notify_rc != OK && notify_rc != ERR_DEVICE_NOT_FOUND) {
        fprintf(stderr,
                "[cleanup] Warning: failed to notify parent %d about dead child %d\n",
                parent_id, dead_id);
    }

    rc = routing_remove_node(dead_id);
    if (rc != OK && rc != ERR_DEVICE_NOT_FOUND) {
        fprintf(stderr, "[cleanup] Warning: failed to remove routing node for %d\n", dead_id);
    }

    controller_remove_device_fifo(&dead_device);
    controller_remove_device_at_index(ctrl, index);

    rc = write_registry(ctrl);
    if (rc != OK) {
        fprintf(stderr, "[cleanup] Failed to update registry\n");
        fflush(stderr);
        return rc;
    }

    fflush(stderr);
    return OK;
}

static int controller_find_device_index_by_pid(const controller *ctrl, pid_t pid)
{
    int i;

    if (ctrl == NULL) {
        return -1;
    }

    for (i = 0; i < ctrl->device_count; ++i) {
        if (ctrl->devices[i].info.pid == pid) {
            return i;
        }
    }

    return -1;
}

int write_registry(const controller *controller)
{
    FILE *fp;
    int i;
    int rc;
    char tmp_path[512];

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = ensure_runtime_dirs();
    if (rc != OK) {
        return rc;
    }

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", REGISTRY_FILE);

    fp = fopen(tmp_path, "w");
    if (fp == NULL) {
        return ERR_SYSTEM;
    }

    for (i = 0; i < controller->device_count; ++i) {
        const device *dev = &controller->devices[i];

        if (fprintf(fp, "%d %d %s\n",
                    dev->info.id,
                    (int)dev->info.pid,
                    dev->info.fifo_path) < 0) {
            fclose(fp);
            unlink(tmp_path);
            return ERR_SYSTEM;
        }
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        unlink(tmp_path);
        return ERR_SYSTEM;
    }

    if (fclose(fp) != 0) {
        unlink(tmp_path);
        return ERR_SYSTEM;
    }

    if (rename(tmp_path, REGISTRY_FILE) != 0) {
        unlink(tmp_path);
        return ERR_SYSTEM;
    }

    return OK;
}


static int controller_send_del_message(const device *dev) {
    domo_message req;

    if(dev == NULL) {
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

static int controller_wait_device_exit(pid_t pid, int *status_out)
{
    int status = 0;
    int waited = 0;

    if (pid <= 0 || status_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    while (waited < TIMEOUT_DEVICE) {
        pid_t w = waitpid(pid, &status, WNOHANG);

        if (w == pid) {
            *status_out = status;
            return OK;
        }

        if (w == 0) {
            sleep(1);
            waited++;
            continue;
        }

        if (w < 0) {
            if (errno == ECHILD) {
                *status_out = 0;
                return OK;
            }
            return ERR_SYSTEM;
        }
    }

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        return ERR_SYSTEM;
    }

    for (int i = 0; i < 3; i++) {
        pid_t w = waitpid(pid, &status, WNOHANG);

        if (w == pid) {
            *status_out = status;
            return OK;
        }

        if (w < 0) {
            if (errno == ECHILD) {
                *status_out = 0;
                return OK;
            }
            return ERR_SYSTEM;
        }

        sleep(1);
    }

    if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
        return ERR_SYSTEM;
    }

    if (waitpid(pid, &status, 0) < 0) {
        if (errno == ECHILD) {
            *status_out = 0;
            return OK;
        }
        return ERR_SYSTEM;
    }

    *status_out = status;
    return OK;
}

static int controller_notify_parent_child_removed(const controller *controller,
                                                  device_id child_id,
                                                  int parent_id)
{
    const device *parent;
    domo_message msg;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (parent_id == CONTROLLER_ID) {
        return OK;
    }

    parent = controller_find_device_const(controller, parent_id);
    if (parent == NULL) {
        return OK;
    }

    memset(&msg, 0, sizeof(msg));
    msg.kind = MSG_REQUEST;
    snprintf(msg.sender_id, sizeof(msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(msg.command, sizeof(msg.command), "CHILD_REMOVED");
    msg.target_id = parent_id;
    msg.src_id = CONTROLLER_ID;
    msg.dst_id = parent_id;
    msg.src_pid = getpid();
    msg.request_id = (int)child_id;
    snprintf(msg.payload, sizeof(msg.payload), "%d", child_id);

    rc = send_message_to_fifo(parent->info.fifo_path, &msg);
    if (rc == ERR_DEVICE_NOT_FOUND || rc == ERR_IPC_FAILURE) {
        return OK;
    }

    return rc;
}


static int controller_delete_children_cascade(controller *controller, device_id parent_id) 
{
    device_id children[CONTROLLER_MAX_DEVICES];
    int count = 0;
    int rc;
    int i;

    // recursively deleteing all children
    rc = routing_collect_children(parent_id, children, CONTROLLER_MAX_DEVICES, &count);
    if(rc != OK) {
        return rc;
    }

    for(i = 0; i < count; i++) {
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
        // child process - exec bulb
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

    if(pid == 0){
        execl("./bin/domotics_controller","controller","--device-window",id_arg,(char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out=pid;
    return OK ;
}

static int spawn_fridge_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32] ;

    snprintf(id_arg,sizeof(id_arg), "%d", id);

    pid =fork() ;
    if (pid <0) {
        return ERR_SYSTEM;
    }

    if(pid==0){
        // child process - exec fridge
        execl("./bin/domotics_controller","controller","--device-fridge",id_arg,(char *)NULL);
        _exit(ERR_SYSTEM);
    }
    *pid_out= pid;


    return OK;
}

static int spawn_hub_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d",id);

    pid = fork();
    if (pid < 0){
        return ERR_SYSTEM;
    }

    if(pid == 0){
        execl("./bin/domotics_controller","controller","--device-hub",id_arg,(char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out=pid;
    return OK;
}

static int spawn_timer_process(device_id id, pid_t *pid_out) {
    pid_t pid ;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork() ;
    if (pid <0)
	{
        return ERR_SYSTEM;
    }

    if(pid == 0){
        execl("./bin/domotics_controller","controller","--device-timer",id_arg,(char *)NULL);
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

	routing_init();
    return ensure_runtime_dirs();
}

static int controller_count_direct_children(const controller *controller, device_id id)
{
    device_id children[CONTROLLER_MAX_DEVICES];
    int count = 0;
    int rc;

    if (controller == NULL) {
        return 0;
    }

    (void)controller;

    rc = routing_collect_children(id, children, CONTROLLER_MAX_DEVICES, &count);
    if (rc != OK) {
        return 0;
    }

    return count;
}

int controller_add_device(controller *controller, device_type type) {
    device *dev;
	device_id id;
    pid_t pid = 0;
	int routing_added=0;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if(controller->device_count >= CONTROLLER_MAX_DEVICES) {
        return ERR_NOT_ALLOWED;
    }

    dev=&controller->devices[controller->device_count];
    memset(dev, 0, sizeof(*dev));

	id = controller ->next_device_id++;
    rc= device_common_init(dev,id,type);
    if(rc!=OK)
    {
        return rc;
    }

    dev->info.logical_parent_id = CONTROLLER_ID;

    switch (type) {
        case DEVICE_BULB:
            rc=spawn_bulb_process(dev->info.id, &pid);
            break;
        case DEVICE_WINDOW:
            rc= spawn_window_process(dev->info.id , &pid) ;
            break;
        case DEVICE_FRIDGE:
            rc= spawn_fridge_process(dev->info.id , &pid);
            break;
        case DEVICE_HUB:
            rc=spawn_hub_process(dev->info.id, &pid);
            break;
        case DEVICE_TIMER:
            rc= spawn_timer_process(dev->info.id, &pid);
            break;
        default:
            return ERR_DEVICE_TYPE_MISMATCH;
    }

    if (rc != OK) {
        return rc;
    }

    dev->info.pid = pid;
    
        // Wait for the device process to open its FIFO reader before returning(?)
    rc=wait_device_fifo_ready(dev->info.fifo_path);
	if(rc!=OK){
		controller_add_rollback(id, pid, routing_added);;
		memset(dev, 0, sizeof(*dev));;
		dev->info.pid=0;
		return rc;
    }


	

	rc = routing_add_node(id, type);
	if(rc!=OK){
		controller_add_rollback(id, pid, routing_added);;
		memset(dev, 0, sizeof(*dev));;
		dev->info.pid=0;
		return rc;
	}
	routing_added=1;

    controller->device_count++;

	
	rc =write_registry(controller);
    if(rc != OK) {
	controller->device_count--;
        controller_add_rollback(id, pid, routing_added);
        memset(dev, 0,sizeof(*dev));
        return rc ;
    }

    printf("Added device: id=%d type=%s pid=%d\n",
           dev->info.id, device_type_str(dev->info.type), (int)dev->info.pid);

    return OK;
}

int controller_delete_device(controller *ctrl, device_id id)
{
    device *dev;
    pid_t pid;
    int status = 0;
    int rc;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device(ctrl, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if (is_control_device(dev->info.type)) {
        rc = controller_delete_children_cascade(ctrl, id);
        if (rc != OK) {
            return rc;
        }
    }

    pid = dev->info.pid;

    rc = controller_send_del_message(dev);
    if (rc != OK) {
        if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
            return ERR_SYSTEM;
        }
    }

    rc = controller_wait_device_exit(pid, &status);
    if (rc != OK) {
        return rc;
    }

    if (controller_find_device_index_by_id(ctrl, id) >= 0) {
        rc = controller_finalize_dead_device(ctrl, pid, status);
        if (rc != OK && rc != ERR_DEVICE_NOT_FOUND) {
            return rc;
        }
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

int controller_info_device(controller *controller, device_id id)
{
    const device *dev;
    domo_message req;
    domo_message resp;
    char reply_fifo[PATH_MAX];
    int rc;
    int target_child_count;
    int timeout_sec;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_INFO);
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.target_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID);
    snprintf(req.payload, sizeof(req.payload), "ALL");

    rc = make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != OK) {
        return rc;
    }

    target_child_count = controller_count_direct_children(controller, id);
    timeout_sec = compute_request_timeout(dev->info.type, target_child_count);

    rc = request_reply_timeout(dev->info.fifo_path, reply_fifo, &req, &resp, timeout_sec);
    if (rc != OK) {
        if (device_is_control(dev->info.type)) {
            printf("%s id=%d state=manual_override error=child_unreachable\n",
                   device_type_str(dev->info.type), id);
            return OK;
        }
        return rc;
    }

    if (resp.status != OK) {
        if (device_is_control(dev->info.type)) {
            printf("%s id=%d state=manual_override error=consistency_check_failed\n",
                   device_type_str(dev->info.type), id);
            return OK;
        }
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
    int timeout_sec;
    int target_child_count;

    if(controller == NULL || label == NULL || pos == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    target_child_count = controller_count_direct_children(controller, id);
    timeout_sec = compute_request_timeout(dev->info.type, target_child_count);

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_SWITCH);
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.target_id = id;
    req.src_pid = getpid();
    req.request_id = (int)getpid();
    snprintf(req.arg1, sizeof(req.arg1), "%s", label);
    snprintf(req.arg2, sizeof(req.arg2), "%s", pos);

    snprintf( req.sender_id,sizeof(req.sender_id), "%d", CONTROLLER_ID); 
     snprintf( req.payload,sizeof(req.payload),"%s,%s",label, pos);


    rc = make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo));
    if(rc != OK) {
        return rc;
    }

    rc = request_reply_timeout(dev->info.fifo_path, reply_fifo, &req, &resp, timeout_sec);
    if(rc != OK) {
        return rc;
    }

    if (resp.status != OK) {
        return resp.status;
    }

    printf("%s\n", resp.payload[0] ? resp.payload : "switch ok");
    return OK;
}

int controller_link_devices(controller *controller, device_id child_id, device_id parent_id)
{
    device *child;
    const device *parent;
    domo_message child_msg;
    domo_message parent_msg;
    int rc;
    int old_parent_id;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    child = controller_find_device(controller, child_id);
    if (child == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    parent = controller_find_device_const(controller, parent_id);
    if (parent == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if (!is_control_device(parent->info.type)) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    old_parent_id = child->info.logical_parent_id;

    rc = routing_link_devices(child_id, parent_id);
    if (rc != OK) {
        return rc;
    }

    memset(&child_msg, 0, sizeof(child_msg));
    child_msg.kind = MSG_REQUEST;
    snprintf(child_msg.sender_id, sizeof(child_msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(child_msg.command, sizeof(child_msg.command), "%s", CMD_LINK);
    child_msg.src_id = CONTROLLER_ID;
    child_msg.dst_id = child_id;
    child_msg.target_id = child_id;
    child_msg.src_pid = getpid();
    child_msg.request_id = (int)getpid();
    snprintf(child_msg.payload, sizeof(child_msg.payload), "parent,%d", parent_id);

    rc = send_message_to_fifo(child->info.fifo_path, &child_msg);
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    memset(&parent_msg, 0, sizeof(parent_msg));
    parent_msg.kind = MSG_REQUEST;
    snprintf(parent_msg.sender_id, sizeof(parent_msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(parent_msg.command, sizeof(parent_msg.command), "CHILD_ADDED");
    parent_msg.src_id = CONTROLLER_ID;
    parent_msg.dst_id = parent_id;
    parent_msg.target_id = parent_id;
    parent_msg.src_pid = getpid();
    parent_msg.request_id = (int)child_id;
    snprintf(parent_msg.payload, sizeof(parent_msg.payload), "%d", child_id);

    rc = send_message_to_fifo(parent->info.fifo_path, &parent_msg);
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    child->info.logical_parent_id = parent_id;

    rc = write_registry(controller);
    if (rc != OK) {
        child->info.logical_parent_id = old_parent_id;
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    printf("Linked device %d to %d\n", child_id, parent_id);
    return OK;
}

int controller_destroy(controller *controller)
{
    int changed;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    do {
        changed = 0;
        for (int i = 0; i < controller->device_count; ++i) {
            device *dev = &controller->devices[i];
            if (dev->info.pid != 0) {
                rc = controller_delete_device(controller, dev->info.id);
                if (rc != OK) {
                    return rc;
                }
                changed = 1;
                break;
            }
        }
    } while (changed);

    rc = write_registry(controller);
    if (rc != OK) {
        return rc;
    }

    return OK;
}

int controller_run(controller *ctrl) {
    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    printf("Domotics controller started.\n");
    printf("Type 'help' for available commands.\n");

    return event_loop_run(ctrl);
}

static int compute_control_timeout(int child_count)
{
    if (child_count < 0) {
        child_count = 0;
    }

    return MIN_RANDOM_DELAY_S + (MAX_RANDOM_DELAY_S * (child_count + 1));
}

static int compute_request_timeout(device_type type, int child_count)
{
    if (type == DEVICE_HUB || type == DEVICE_TIMER || type == DEVICE_CONTROLLER) {
        return compute_control_timeout(child_count);
    }

    return TIMEOUT_DEVICE;
}