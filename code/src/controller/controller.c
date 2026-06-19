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
#include <time.h>

#include "controller.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "routing.h"
#include "event_loop.h"
#include "device.h"
#include "cleanup.h"

static int ensure_runtime_dirs(void) {
    if (mkdir(RUNTIME_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if (mkdir(FIFO_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if (mkdir(LOG_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if (mkdir(PID_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
    if (mkdir(REGISTRY_DIR, 0777) != 0 && errno != EEXIST) return ERR_SYSTEM;
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

static int controller_next_request_id(void);
static pending_request *controller_alloc_pending(controller *ctrl);
static pending_request *controller_find_pending_by_fd(controller *ctrl, int reply_fd);
static void controller_clear_pending(pending_request *req);

static int fifo_not_ready_errno(int err)
{
    return err == ENOENT || err == ENXIO;
}

static int wait_device_fifo_ready(const char *fifo_path) {
    int retries = TIMEOUT_DEVICE;

    while (retries-- > 0) {
        int ready_fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
        if (ready_fd >= 0) {
            close(ready_fd);
            return OK;
        }
        if (!fifo_not_ready_errno(errno)) {
            return ERR_SYSTEM;
        }
        sleep(1);
    }

    return ERR_TIMEOUT;
}

static void controller_add_rollback(device_id id, pid_t pid, int routing_added) {
    char fifo_path[PATH_MAX];

    if (pid > 0) {
        waitpid(pid, NULL, WNOHANG);
    }

    if (routing_added) {
        routing_remove_node(id);
    }

    if (make_device_fifo_path(id, fifo_path, sizeof(fifo_path)) == OK) {
        unlink(fifo_path);
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

    (void)status;  //debug log(da togliere in teoria) Parameter unused after debug logs commented out

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

    // Debug log - commented out
    /**
     * Quando: Quando un dispositivo crasha o termina
Cosa stampa: ID, PID, segnale di terminazione, tipo di dispositivo
     */
    // if (WIFSIGNALED(status)) {
    //     fprintf(stderr,
    //             "\n[cleanup] Device crashed: id=%d pid=%ld signal=%d type=%s\n",
    //             dead_device.info.id,
    //             (long)dead_pid,
    //             WTERMSIG(status),
    //             device_type_str(dead_device.info.type));
    // } else if (WIFEXITED(status)) {
    //     fprintf(stderr,
    //             "\n[cleanup] Device exited: id=%d pid=%ld exit_status=%d type=%s\n",
    //             dead_device.info.id,
    //             (long)dead_pid,
    //             WEXITSTATUS(status),
    //             device_type_str(dead_device.info.type));
    // } else {
    //     fprintf(stderr,
    //             "\n[cleanup] Device terminated: id=%d pid=%ld type=%s\n",
    //             dead_device.info.id,
    //             (long)dead_pid,
    //             device_type_str(dead_device.info.type));
    // }

    notify_rc = controller_notify_parent_child_removed(ctrl, dead_id, parent_id);
    if (notify_rc != OK && notify_rc != ERR_DEVICE_NOT_FOUND) {
        // Debug log - commented out
        // fprintf(stderr,
        //         "[cleanup] Warning: failed to notify parent %d about dead child %d\n",
        //         parent_id, dead_id);
    }

    rc = routing_remove_node(dead_id);
    if (rc != OK && rc != ERR_DEVICE_NOT_FOUND) {
        // Debug log - commented out
        // fprintf(stderr, "[cleanup] Warning: failed to remove routing node for %d\n", dead_id);
    }

    controller_remove_device_fifo(&dead_device);
    controller_remove_device_at_index(ctrl, index);

    rc = write_registry(ctrl);
    if (rc != OK) {
        // Debug log - commented out
        // fprintf(stderr, "[cleanup] Failed to update registry\n");
        // fflush(stderr);
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

    return ERR_TIMEOUT;
}

static int controller_notify_parent_child_removed(const controller *controller,
                                                  device_id child_id,
                                                  int parent_id)
{
    const device *parent;
    domo_message msg;

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
    snprintf(msg.command, sizeof(msg.command), "%s", CMD_CHILD_REMOVED);
    msg.target_id = parent_id;
    msg.src_id = CONTROLLER_ID;
    msg.dst_id = parent_id;
    msg.src_pid = getpid();
    msg.request_id = (int)child_id;
    snprintf(msg.payload, sizeof(msg.payload), "%d", child_id);

    (void)send_message_to_fifo(parent->info.fifo_path, &msg);
    return OK;
}

static int controller_delete_children_cascade(controller *ctrl, device_id parent_id)
{
    device_id children[CONTROLLER_MAX_DEVICES];
    int count = 0;
    int rc;
    int i;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = routing_collect_children(parent_id, children, CONTROLLER_MAX_DEVICES, &count);
    if (rc != OK) {
        return rc;
    }

    for (i = 0; i < count; ++i) {
        const device *child = controller_find_device_const(ctrl, children[i]);

        if (child == NULL) {
            continue;
        }

        rc = controller_send_del_message(child);
        if (rc != OK &&
            rc != ERR_DEVICE_NOT_FOUND &&
            rc != ERR_IPC_FAILURE) {
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

    if (pid == 0) {
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
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if (pid == 0) {
        execl("./bin/domotics_controller", "controller", "--device-window", id_arg, (char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}

static int spawn_fridge_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if (pid == 0) {
        execl("./bin/domotics_controller", "controller", "--device-fridge", id_arg, (char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}

static int spawn_hub_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if (pid == 0) {
        execl("./bin/domotics_controller", "controller", "--device-hub", id_arg, (char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}

static int spawn_timer_process(device_id id, pid_t *pid_out) {
    pid_t pid;
    char id_arg[32];

    snprintf(id_arg, sizeof(id_arg), "%d", id);

    pid = fork();
    if (pid < 0) {
        return ERR_SYSTEM;
    }

    if (pid == 0) {
        execl("./bin/domotics_controller", "controller", "--device-timer", id_arg, (char *)NULL);
        _exit(ERR_SYSTEM);
    }

    *pid_out = pid;
    return OK;
}

device *controller_find_device(controller *controller, device_id id) {
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

static int controller_next_request_id(void)
{
    static int next_id = 1;

    if (next_id <= 0) {
        next_id = 1;
    }

    return next_id++;
}

static pending_request *controller_alloc_pending(controller *ctrl)
{
    int i;

    if (ctrl == NULL) {
        return NULL;
    }

    for (i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
        if (!ctrl->pending[i].in_use) {
            memset(&ctrl->pending[i], 0, sizeof(ctrl->pending[i]));
            ctrl->pending[i].in_use = 1;
            ctrl->pending[i].reply_fd = -1;
            return &ctrl->pending[i];
        }
    }

    return NULL;
}

static pending_request *controller_find_pending_by_fd(controller *ctrl, int reply_fd)
{
    int i;

    if (ctrl == NULL || reply_fd < 0) {
        return NULL;
    }

    for (i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
        if (ctrl->pending[i].in_use && ctrl->pending[i].reply_fd == reply_fd) {
            return &ctrl->pending[i];
        }
    }

    return NULL;
}

static void controller_clear_pending(pending_request *req)
{
    if (req == NULL) {
        return;
    }

    if (req->reply_fd >= 0) {
        close(req->reply_fd);
    }

    if (req->reply_fifo_path[0] != '\0') {
        unlink(req->reply_fifo_path);
    }

    memset(req, 0, sizeof(*req));
    req->reply_fd = -1;
}

static int controller_collect_subtree(controller *ctrl,
                                      device_id root_id,
                                      device_id *ids,
                                      int max_ids,
                                      int *count_out)
{
    device_id children[CONTROLLER_MAX_DEVICES];
    int child_count = 0;
    int rc;
    int i;
    int total = 0;

    if (ctrl == NULL || ids == NULL || count_out == NULL || max_ids <= 0) {
        return ERR_INVALID_PARAMETERS;
    }

    ids[total++] = root_id;

    rc = routing_collect_children(root_id, children, CONTROLLER_MAX_DEVICES, &child_count);
    if (rc != OK) {
        return rc;
    }

    for (i = 0; i < child_count; ++i) {
        if (total >= max_ids) {
            return ERR_NOT_ALLOWED;
        }
        ids[total++] = children[i];
    }

    *count_out = total;
    return OK;
}

static int controller_wait_subtree_removed(controller *ctrl,
                                           const device_id *ids,
                                           int count)
{
    int waited = 0;

    if (ctrl == NULL || ids == NULL || count < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    while (waited < TIMEOUT_DEVICE + MAX_RANDOM_DELAY_S + 2) {
        int all_gone = 1;
        int i;

        cleanup_reap_terminated_children(ctrl);

        for (i = 0; i < count; ++i) {
            if (controller_find_device_index_by_id(ctrl, ids[i]) >= 0) {
                all_gone = 0;
                break;
            }
        }

        if (all_gone) {
            return OK;
        }

        sleep(1);
        waited++;
    }

    return ERR_TIMEOUT;
}

int controller_init(controller *controller)
{
    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(controller, 0, sizeof(*controller));
    controller->running = 1;
    controller->next_device_id = 1;

    for (int i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
        controller->pending[i].reply_fd = -1;
    }

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
        case DEVICE_HUB:
            rc = spawn_hub_process(dev->info.id, &pid);
            break;
        case DEVICE_TIMER:
            rc = spawn_timer_process(dev->info.id, &pid);
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

int controller_delete_device(controller *ctrl, device_id id)
{
    device *dev;
    pid_t pid;
    int status = 0;
    int rc;
    device_id subtree_ids[CONTROLLER_MAX_DEVICES];
    int subtree_count = 0;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device(ctrl, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    rc = controller_collect_subtree(ctrl, id,
                                    subtree_ids,
                                    CONTROLLER_MAX_DEVICES,
                                    &subtree_count);
    if (rc != OK) {
        return rc;
    }

    if (device_is_control(dev->info.type)) {
        rc = controller_delete_children_cascade(ctrl, id);
        if (rc != OK) {
            return rc;
        }
    }

    pid = dev->info.pid;

    rc = controller_send_del_message(dev);
    if (rc != OK) {
        return rc;
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

    rc = controller_wait_subtree_removed(ctrl, subtree_ids, subtree_count);
    if (rc != OK) {
        return rc;
    }

    printf("Deleted device: id=%d\n", id);
    return OK;
}

int controller_list_devices(controller *controller) {
    int i;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    printf("ID\tTYPE\tPID\tSTATE\tPARENT\n");
    for (i = 0; i < controller->device_count; ++i) {
        device *dev = &controller->devices[i];
        if (dev->info.pid == 0) {
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
    pending_request *pend;
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

    pend = controller_alloc_pending(controller);
    if (pend == NULL) {
        return ERR_NOT_ALLOWED;
    }

    pend->request_id = controller_next_request_id();
    rc = make_reply_fifo_path(getpid(), pend->request_id,
                              pend->reply_fifo_path, sizeof(pend->reply_fifo_path));
    if (rc != OK) {
        controller_clear_pending(pend);
        return rc;
    }

    unlink(pend->reply_fifo_path);
    if (mkfifo(pend->reply_fifo_path, 0666) != 0 && errno != EEXIST) {
        controller_clear_pending(pend);
        return ERR_SYSTEM;
    }

    pend->reply_fd = open(pend->reply_fifo_path, O_RDONLY | O_NONBLOCK);
    if (pend->reply_fd < 0) {
        controller_clear_pending(pend);
        return ERR_SYSTEM;
    }

    target_child_count = controller_count_direct_children(controller, id);
    timeout_sec = compute_request_timeout(dev->info.type, target_child_count);

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_INFO);
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID);
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.target_id = id;
    req.src_pid = getpid();
    req.request_id = pend->request_id;
    snprintf(req.payload, sizeof(req.payload), "ALL");

    pend->kind = CTRL_REQ_INFO;
    pend->target_id = id;
    pend->target_type = dev->info.type;
    pend->deadline = time(NULL) + timeout_sec;

    rc = send_message_to_fifo(dev->info.fifo_path, &req);
    if (rc != OK) {
        controller_clear_pending(pend);
        return rc;
    }

    printf("[pending] info %d\n", id);
    return OK;
}

int controller_switch_device(controller *controller, device_id id, const char *label, const char *pos)
{
    const device *dev;
    domo_message req;
    pending_request *pend;
    int rc;
    int timeout_sec;
    int target_child_count;

    if (controller == NULL || label == NULL || pos == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    dev = controller_find_device_const(controller, id);
    if (dev == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    pend = controller_alloc_pending(controller);
    if (pend == NULL) {
        return ERR_NOT_ALLOWED;
    }

    pend->request_id = controller_next_request_id();
    rc = make_reply_fifo_path(getpid(), pend->request_id,
                              pend->reply_fifo_path, sizeof(pend->reply_fifo_path));
    if (rc != OK) {
        controller_clear_pending(pend);
        return rc;
    }

    unlink(pend->reply_fifo_path);
    if (mkfifo(pend->reply_fifo_path, 0666) != 0 && errno != EEXIST) {
        controller_clear_pending(pend);
        return ERR_SYSTEM;
    }

    pend->reply_fd = open(pend->reply_fifo_path, O_RDONLY | O_NONBLOCK);
    if (pend->reply_fd < 0) {
        controller_clear_pending(pend);
        return ERR_SYSTEM;
    }

    target_child_count = controller_count_direct_children(controller, id);
    timeout_sec = compute_request_timeout(dev->info.type, target_child_count);

    memset(&req, 0, sizeof(req));
    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", CMD_SWITCH);
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", CONTROLLER_ID);
    req.src_id = CONTROLLER_ID;
    req.dst_id = id;
    req.target_id = id;
    req.src_pid = getpid();
    req.request_id = pend->request_id;
    snprintf(req.arg1, sizeof(req.arg1), "%s", label);
    snprintf(req.arg2, sizeof(req.arg2), "%s", pos);
    snprintf(req.payload, sizeof(req.payload), "%s,%s", label, pos);

    pend->kind = CTRL_REQ_SWITCH;
    pend->target_id = id;
    pend->target_type = dev->info.type;
    pend->deadline = time(NULL) + timeout_sec;
    snprintf(pend->extra1, sizeof(pend->extra1), "%s", label);
    snprintf(pend->extra2, sizeof(pend->extra2), "%s", pos);

    rc = send_message_to_fifo(dev->info.fifo_path, &req);
    if (rc != OK) {
        controller_clear_pending(pend);
        return rc;
    }

    printf("[pending] switch %d %s %s\n", id, label, pos);
    return OK;
}

int controller_complete_pending_fd(controller *ctrl, int reply_fd)
{
    pending_request *pend;
    domo_message msg;
    int rc;

    if (ctrl == NULL || reply_fd < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    pend = controller_find_pending_by_fd(ctrl, reply_fd);
    if (pend == NULL) {
        return ERR_DEVICE_NOT_FOUND;
    }

    memset(&msg, 0, sizeof(msg));
    rc = ipc_recv_message(reply_fd, &msg);
    if (rc != OK) {
        return rc;
    }

    if (pend->kind == CTRL_REQ_INFO) {
        if (msg.status != OK) {
            if (device_is_control(pend->target_type)) {
                printf("%s id=%d state=manual_override error=consistency_check_failed\n",
                       device_type_str(pend->target_type),
                       pend->target_id);
            } else {
                printf("info %d failed: %s\n",
                       pend->target_id,
                       error_str(msg.status));
            }
        } else {
            printf("%s\n", msg.payload);
        }
    } else if (pend->kind == CTRL_REQ_SWITCH) {
        if (msg.status != OK) {
            printf("switch %d failed: %s\n",
                   pend->target_id,
                   error_str(msg.status));
        } else {
            printf("%s\n", msg.payload[0] ? msg.payload : "switch ok");
        }
    } else {
        controller_clear_pending(pend);
        return ERR_INVALID_PARAMETERS;
    }

    controller_clear_pending(pend);
    return OK;
}

int controller_expire_pending(controller *ctrl)
{
    int i;
    time_t now;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    now = time(NULL);

    for (i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
        pending_request *pend = &ctrl->pending[i];

        if (!pend->in_use) {
            continue;
        }

        if (pend->deadline <= now) {
            if (pend->kind == CTRL_REQ_INFO && device_is_control(pend->target_type)) {
                printf("%s id=%d state=manual_override error=child_unreachable\n",
                       device_type_str(pend->target_type),
                       pend->target_id);
            } else if (pend->kind == CTRL_REQ_INFO) {
                printf("info %d failed: %s\n",
                       pend->target_id,
                       error_str(ERR_TIMEOUT));
            } else if (pend->kind == CTRL_REQ_SWITCH) {
                printf("switch %d failed: %s\n",
                       pend->target_id,
                       error_str(ERR_TIMEOUT));
            }

            controller_clear_pending(pend);
        }
    }

    return OK;
}

int controller_link_devices(controller *controller, device_id child_id, device_id parent_id)
{
    device *child;
    const device *parent;
    domo_message child_msg;
    domo_message parent_msg;
    domo_message child_resp;
    domo_message parent_resp;
    char reply_fifo[PATH_MAX];
    int rc;
    int old_parent_id;
    int request_id;

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

    if (!device_is_control(parent->info.type)) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    old_parent_id = child->info.logical_parent_id;

    rc = routing_link_devices(child_id, parent_id);
    if (rc != OK) {
        return rc;
    }

    memset(&child_msg, 0, sizeof(child_msg));
    memset(&child_resp, 0, sizeof(child_resp));

    request_id = controller_next_request_id();

    child_msg.kind = MSG_REQUEST;
    snprintf(child_msg.sender_id, sizeof(child_msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(child_msg.command, sizeof(child_msg.command), "%s", CMD_LINK);
    child_msg.src_id = CONTROLLER_ID;
    child_msg.dst_id = child_id;
    child_msg.target_id = child_id;
    child_msg.src_pid = getpid();
    child_msg.request_id = request_id;
    snprintf(child_msg.payload, sizeof(child_msg.payload), "parent,%d", parent_id);

    rc = make_reply_fifo_path(getpid(), request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    rc = request_reply_timeout(child->info.fifo_path, reply_fifo,
                               &child_msg, &child_resp, TIMEOUT_DEVICE);
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    if (child_resp.status != OK) {
        routing_link_devices(child_id, old_parent_id);
        return child_resp.status;
    }

    memset(&parent_msg, 0, sizeof(parent_msg));
    memset(&parent_resp, 0, sizeof(parent_resp));

    request_id = controller_next_request_id();

    parent_msg.kind = MSG_REQUEST;
    snprintf(parent_msg.sender_id, sizeof(parent_msg.sender_id), "%d", CONTROLLER_ID);
    snprintf(parent_msg.command, sizeof(parent_msg.command), "%s", CMD_CHILD_ADDED);
    parent_msg.src_id = CONTROLLER_ID;
    parent_msg.dst_id = parent_id;
    parent_msg.target_id = parent_id;
    parent_msg.src_pid = getpid();
    parent_msg.request_id = request_id;
    snprintf(parent_msg.payload, sizeof(parent_msg.payload), "%d", child_id);

    rc = make_reply_fifo_path(getpid(), request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    rc = request_reply_timeout(parent->info.fifo_path, reply_fifo,
                               &parent_msg, &parent_resp, TIMEOUT_DEVICE);
    if (rc != OK) {
        routing_link_devices(child_id, old_parent_id);
        return rc;
    }

    if (parent_resp.status != OK) {
        routing_link_devices(child_id, old_parent_id);
        return parent_resp.status;
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