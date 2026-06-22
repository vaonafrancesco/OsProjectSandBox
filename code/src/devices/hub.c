#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "routing.h"
#include "utils.h"

#ifndef MAX_DEVICES
#define MAX_DEVICES 128
#endif

typedef struct {
    device base;
    bool manual_override;
    device_id children[MAX_DEVICES];
    int child_count;
    int next_request_id;
} hub_device;

static const char *state_str(state s)
{
    switch (s) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int hub_next_request_id(hub_device *hub)
{
    if (hub == NULL) {
        return 1;
    }

    if (hub->next_request_id <= 0) {
        hub->next_request_id = 1;
    }

    return hub->next_request_id++;
}

static int hub_add_child_local(hub_device *hub, device_id child_id)
{
    int i;

    if (hub == NULL || child_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < hub->child_count; ++i) {
        if (hub->children[i] == child_id) {
            return OK;
        }
    }

    if (hub->child_count >= MAX_DEVICES) {
        return ERR_NOT_ALLOWED;
    }

    hub->children[hub->child_count++] = child_id;
    hub->base.child_count = (size_t)hub->child_count;
    return OK;
}

static int hub_remove_child_local(hub_device *hub, device_id child_id)
{
    int i;

    if (hub == NULL || child_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < hub->child_count; ++i) {
        if (hub->children[i] == child_id) {
            hub->children[i] = hub->children[hub->child_count - 1];
            hub->child_count--;
            hub->base.child_count = (size_t)hub->child_count;
            return OK;
        }
    }

    return ERR_DEVICE_NOT_FOUND;
}

static int parse_child_id_payload(const char *payload, device_id *child_id_out)
{
    int id;

    if (payload == NULL || child_id_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (sscanf(payload, "%d", &id) != 1 || id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *child_id_out = (device_id)id;
    return OK;
}

static int parse_link_parent_id(const domo_message *req, int *parent_id_out)
{
    int parent_id;

    if (req == NULL || parent_id_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (sscanf(req->payload, "parent,%d", &parent_id) != 1) {
        return ERR_INVALID_PARAMETERS;
    }

    if (parent_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *parent_id_out = parent_id;
    return OK;
}

static int hub_load_children(hub_device *hub)
{
    int count = 0;
    int rc;

    if (hub == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (hub->child_count > 0) {
        return OK;
    }

    memset(hub->children, 0, sizeof(hub->children));
    hub->child_count = 0;

    if (hub->base.child_ids != NULL && hub->base.child_count > 0) {
        size_t n = hub->base.child_count;
        if (n > MAX_DEVICES) {
            n = MAX_DEVICES;
        }

        for (size_t i = 0; i < n; ++i) {
            hub->children[i] = hub->base.child_ids[i];
        }

        hub->child_count = (int)n;
        hub->base.child_count = (size_t)hub->child_count;
        return OK;
    }

    rc = routing_collect_children(hub->base.info.id,
                                  hub->children,
                                  MAX_DEVICES,
                                  &count);
    if (rc != OK) {
        return rc;
    }

    if (count < 0 || count > MAX_DEVICES) {
        return ERR_IPC_FAILURE;
    }

    hub->child_count = count;
    hub->base.child_count = (size_t)hub->child_count;
    return OK;
}

static int parse_switch_args(const domo_message *req,
                             char *label, size_t label_len,
                             char *position, size_t position_len)
{
    char local[MAX_MSG_LEN];
    char *comma;

    if (req == NULL || label == NULL || position == NULL ||
        label_len == 0 || position_len == 0) {
        return ERR_INVALID_PARAMETERS;
    }

    label[0] = '\0';
    position[0] = '\0';

    if (req->arg1[0] != '\0' && req->arg2[0] != '\0') {
        snprintf(label, label_len, "%s", req->arg1);
        snprintf(position, position_len, "%s", req->arg2);
        return OK;
    }

    if (req->payload[0] == '\0') {
        return ERR_INVALID_PARAMETERS;
    }

    snprintf(local, sizeof(local), "%s", req->payload);
    comma = strchr(local, ',');
    if (comma == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    *comma = '\0';
    snprintf(label, label_len, "%s", local);
    snprintf(position, position_len, "%s", comma + 1);

    if (label[0] == '\0' || position[0] == '\0') {
        return ERR_INVALID_PARAMETERS;
    }

    return OK;
}

static int expected_state_from_position(const char *position, state *out_state)
{
    if (position == NULL || out_state == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (strcmp(position, "on") == 0) {
        *out_state = STATE_ON;
        return OK;
    }

    if (strcmp(position, "off") == 0) {
        *out_state = STATE_OFF;
        return OK;
    }

    return ERR_INVALID_PARAMETERS;
}

static int extract_child_state_from_info(const char *payload, state *out_state)
{
    if (payload == NULL || out_state == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (strstr(payload, "state=on") != NULL || strstr(payload, "state=open") != NULL) {
        *out_state = STATE_ON;
        return OK;
    }

    if (strstr(payload, "state=off") != NULL || strstr(payload, "state=closed") != NULL) {
        *out_state = STATE_OFF;
        return OK;
    }

    return ERR_INVALID_STATE;
}

static int hub_send_command_to_child(hub_device *hub,
                                     device_id child_id,
                                     const char *command,
                                     const char *arg1,
                                     const char *arg2,
                                     const char *payload,
                                     domo_message *child_resp)
{
    domo_message req;
    char target_fifo[PATH_MAX];
    char reply_fifo[PATH_MAX];
    int rc;

    if (hub == NULL || command == NULL || child_resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(&req, 0, sizeof(req));
    memset(child_resp, 0, sizeof(*child_resp));

    req.kind = MSG_REQUEST;
    snprintf(req.command, sizeof(req.command), "%s", command);
    snprintf(req.sender_id, sizeof(req.sender_id), "%d", hub->base.info.id);
    req.target_id = child_id;
    req.src_id = hub->base.info.id;
    req.dst_id = child_id;
    req.src_pid = getpid();
    req.request_id = hub_next_request_id(hub);
    req.status = OK;

    if (arg1 != NULL) {
        snprintf(req.arg1, sizeof(req.arg1), "%s", arg1);
    }
    if (arg2 != NULL) {
        snprintf(req.arg2, sizeof(req.arg2), "%s", arg2);
    }
    if (payload != NULL) {
        snprintf(req.payload, sizeof(req.payload), "%s", payload);
    }

    rc = make_device_fifo_path(child_id, target_fifo, sizeof(target_fifo));
    if (rc != OK) {
        return rc;
    }

    rc = make_reply_fifo_path(req.src_pid, req.request_id, reply_fifo, sizeof(reply_fifo));
    if (rc != OK) {
        return rc;
    }

    return request_reply_timeout(target_fifo, reply_fifo, &req, child_resp, TIMEOUT_DEVICE);
}

static int hub_propagate_to_children(device *dev, const domo_message *req)
{
    hub_device *hub = (hub_device *)dev;
    int rc;
    int i;
    char label[32];
    char position[32];
    char payload[96];
    state expected_state;

    if (hub == NULL || req == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = hub_load_children(hub);
    if (rc != OK) {
        return rc;
    }

    rc = parse_switch_args(req, label, sizeof(label), position, sizeof(position));
    if (rc != OK) {
        return rc;
    }

    rc = expected_state_from_position(position, &expected_state);
    if (rc != OK) {
        return rc;
    }

    //in pratica costruisce il payload in maniera universale, bypassa le ettichettte specifiche delle varie foglie(in pratica se ne frega se è open oppure power, il controllo si fa dopo)
    snprintf(payload, sizeof(payload), "sys_state,%s", position);

    for (i = 0; i < hub->child_count; ++i) {
        domo_message child_resp;

        rc = hub_send_command_to_child(hub,
                                       hub->children[i],
                                       CMD_SWITCH,
                                       "sys_state",
                                       position,
                                       payload,
                                       &child_resp);
        if (rc != OK) {
            continue; // La system call ipc è fallita (può essere boh, pipe rotta o figlio morto che ne so)
        }

        if (child_resp.status != OK) {
            return child_resp.status;
        }
    }

    hub->manual_override = false;
    hub->base.info.manual_override = false;
    hub->base.info.state = expected_state;

    return OK;
}

static int hub_check_children_consistency(device *dev, bool *consistent_out)
{
    hub_device *hub = (hub_device *)dev;
    int rc;
    int i;
    bool first_set = false;
    state first_state = STATE_OFF;

    if (hub == NULL || consistent_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    *consistent_out = true;

    rc = hub_load_children(hub);
    if (rc != OK) {
        return rc;
    }

    if (hub->child_count == 0) {
        return OK;
    }

    for (i = 0; i < hub->child_count; ++i) {
        domo_message resp;
        state child_state;

        rc = hub_send_command_to_child(hub,
                                       hub->children[i],
                                       CMD_INFO,
                                       "",
                                       "",
                                       "ALL",
                                       &resp);
        if (rc != OK || resp.status != OK) {
            *consistent_out = false;
            return OK;
        }

        rc = extract_child_state_from_info(resp.payload, &child_state);
        if (rc != OK) {
            *consistent_out = false;
            return OK;
        }

        if (!first_set) {
            first_state = child_state;
            first_set = true;
        } else if (child_state != first_state) {
            *consistent_out = false;
            return OK;
        }
    }

    hub->base.info.state = first_state;
    return OK;
}

static int hub_build_info_payload(hub_device *hub, char *buf, size_t len)
{
    bool consistent = true;
    int rc;

    if (hub == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = hub_check_children_consistency(&hub->base, &consistent);
    if (rc != OK) {
        snprintf(buf, len, "hub id=%d parent=%d state=%s",
                 hub->base.info.id,
                 hub->base.info.logical_parent_id,
                 state_str(hub->base.info.state));
        return OK;
    }

    hub->manual_override = !consistent;
    hub->base.info.manual_override = !consistent;

    if (!consistent) {
        snprintf(buf, len, "hub id=%d parent=%d state=manual_override",
                    hub->base.info.id,
                    hub->base.info.logical_parent_id);
    } else {
        snprintf(buf, len, "hub id=%d parent=%d state=%s",
                 hub->base.info.id,
                 hub->base.info.logical_parent_id,
                 state_str(hub->base.info.state));
    }

    return OK;
}

static int hub_handle_message(device *dev, const domo_message *req, domo_message *resp)
{
    hub_device *hub = (hub_device *)dev;
    int rc;

    if (hub == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(resp, 0, sizeof(*resp));
    resp->kind = MSG_RESPONSE;
    snprintf(resp->command, sizeof(resp->command), "%s", req->command);
    snprintf(resp->sender_id, sizeof(resp->sender_id), "%d", hub->base.info.id);
    resp->src_id = hub->base.info.id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = OK;

    simulate_random_delay();

    if (strcmp(req->command, CMD_INFO) == 0) {
        return hub_build_info_payload(hub, resp->payload, sizeof(resp->payload));
    }

    if (strcmp(req->command, "CHILD_ADDED") == 0) {
        device_id child_id;
        rc = parse_child_id_payload(req->payload, &child_id);
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid child_added payload");
            return OK;
        }

        rc = hub_add_child_local(hub, child_id);
        if (rc != OK) {
            resp->status = rc;
            snprintf(resp->payload, sizeof(resp->payload), "failed to add child");
            return OK;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "hub %d added child %d",
                 hub->base.info.id,
                 child_id);
        return OK;
    }

    if (strcmp(req->command, "CHILD_REMOVED") == 0) {
        device_id child_id;
        rc = parse_child_id_payload(req->payload, &child_id);
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid child_removed payload");
            return OK;
        }

        rc = hub_remove_child_local(hub, child_id);
        if (rc != OK && rc != ERR_DEVICE_NOT_FOUND) {
            resp->status = rc;
            snprintf(resp->payload, sizeof(resp->payload), "failed to remove child");
            return OK;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "hub %d removed child %d",
                 hub->base.info.id,
                 child_id);
        return OK;
    }

    if (strcmp(req->command, CMD_LINK) == 0) {
        int parent_id;
        rc = parse_link_parent_id(req, &parent_id);
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid link payload");
            return OK;
        }

        hub->base.info.logical_parent_id = parent_id;
        snprintf(resp->payload, sizeof(resp->payload),
                 "hub %d linked to parent %d",
                 hub->base.info.id,
                 parent_id);
        return OK;
    }

    if (strcmp(req->command, CMD_SWITCH) == 0) {
        char label[32];
        char position[32];
        state expected_state;

        rc = parse_switch_args(req, label, sizeof(label), position, sizeof(position));
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch payload");
            return OK;
        }

        if (strcmp(label, "power") != 0 && strcmp(label, "sys_state") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        rc = expected_state_from_position(position, &expected_state);
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        hub->base.info.state = expected_state;
        hub->manual_override = false;
        hub->base.info.manual_override = false;

        rc = hub_propagate_to_children(dev, req);
        if (rc != OK) {
            resp->status = rc;
            snprintf(resp->payload, sizeof(resp->payload),
                    "failed to propagate switch to children");
            return OK;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                "hub %d switched %s",
                hub->base.info.id,
                state_str(hub->base.info.state));
        return OK;
    }

    if (strcmp(req->command, CMD_STATUS) == 0) {
        if (strncmp(req->payload, "manual_override,", 16) == 0) {
            hub->manual_override = true;
            hub->base.info.manual_override = true;
            return OK;
        }

        return OK;
    }

    resp->status = ERR_INVALID_COMMAND;
    snprintf(resp->payload, sizeof(resp->payload), "unknown command");
    return OK;
}

static int hub_init(device *dev)
{
    hub_device *hub = (hub_device *)dev;

    if (hub == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    hub->base.info.logical_parent_id=0;
    memset(hub->children, 0, sizeof(hub->children));
    hub->child_count = 0;
    hub->manual_override = false;
    hub->next_request_id = 1;

    hub->base.info.state = STATE_OFF;
    hub->base.info.manual_override = false;

    hub->base.child_ids = hub->children;
    hub->base.child_capacity = MAX_DEVICES;
    hub->base.child_count = 0;

    return OK;
}

static int hub_destroy(device *dev)
{
    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    return OK;
}

int hub_device_main(device_id id)
{
    hub_device hub;
    int fd, dummy_fd;
    int rc;

    memset(&hub, 0, sizeof(hub));

    rc = device_common_init(&hub.base, id, DEVICE_HUB);
    if (rc != OK) {
        return rc;
    }

    hub.base.handle_message = hub_handle_message;
    hub.base.destroy = hub_destroy;

    rc = hub_init(&hub.base);
    if (rc != OK) {
        return rc;
    }

    rc = device_common_setup_fifo(&hub.base);
    if (rc != OK) {
        return rc;
    }

    rc = device_common_open_fifo(&hub.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }

    rc = device_common_main_loop(&hub.base, fd);
    device_common_cleanup(&hub.base, fd, dummy_fd);
    return rc;
}
