#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"
#include "routing.h"

typedef struct {
    device base;
    bool manual_override;
    device_id children[MAX_DEVICES];
    int child_count;
} hub_device;

static const char *state_str(state state) 
{
    switch(state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int hub_add_child(hub_device *hub, device_id child_id)
{
    int i;

    if (hub == NULL || child_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < hub->child_count; i++) {
        if (hub->children[i] == child_id) {
            return OK;
        }
    }

    if (hub->child_count >= MAX_DEVICES) {
        return ERR_NOT_ALLOWED;
    }

    hub->children[hub->child_count++] = child_id;
    return OK;
}

static int hub_remove_child(hub_device *hub, device_id child_id)
{
    int i;

    if (hub == NULL || child_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < hub->child_count; i++) {
        if (hub->children[i] == child_id) {
            int j;
            for (j = i; j < hub->child_count - 1; j++) {
                hub->children[j] = hub->children[j + 1];
            }
            hub->child_count--;
            return OK;
        }
    }

    return ERR_DEVICE_NOT_FOUND;
}

static int parse_switch_args(const domo_message *req,
                             char *label, size_t label_len,
                             char *position, size_t position_len)
{
    const char *comma;

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

    comma = strchr(req->payload, ',');
    if (comma == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    {
        size_t label_size = (size_t)(comma - req->payload);
        if (label_size == 0 || label_size >= label_len) {
            return ERR_INVALID_PARAMETERS;
        }

        memcpy(label, req->payload, label_size);
        label[label_size] = '\0';
    }

    snprintf(position, position_len, "%s", comma + 1);

    if (position[0] == '\0') {
        return ERR_INVALID_PARAMETERS;
    }

    return OK;
}

static int extract_child_state_from_info(const char *payload, state *out_state)
{
    if (payload == NULL || out_state == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (strstr(payload, "state=manual_override") != NULL) {
        return ERR_IPC_FAILURE;
    }

    if (strstr(payload, "state=on") != NULL) {
        *out_state = STATE_ON;
        return OK;
    }

    if (strstr(payload, "state=off") != NULL) {
        *out_state = STATE_OFF;
        return OK;
    }

    return ERR_IPC_FAILURE;
}

static int hub_propagate_to_children(device *dev, const domo_message *req) {
    hub_device *hub = (hub_device *)dev;
    int rc;
    int i;
    char label[32];
    char position[32];

    if (hub == NULL || req == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = parse_switch_args(req, label, sizeof(label), position, sizeof(position));
    if (rc != OK) {
        return rc;
    }

    for (i = 0; i < hub->child_count; i++) {
        domo_message child_req;
        domo_message child_resp;
        char child_fifo[PATH_MAX];
        char reply_fifo[PATH_MAX];

        memset(&child_req, 0, sizeof(child_req));
        memset(&child_resp, 0, sizeof(child_resp));

        child_req.kind = MSG_REQUEST;
        snprintf(child_req.sender_id, sizeof(child_req.sender_id), "%d", dev->info.id);
        snprintf(child_req.command, sizeof(child_req.command), "%s", req->command);
        child_req.src_id = dev->info.id;
        child_req.dst_id = hub->children[i];
        child_req.target_id = hub->children[i];
        child_req.src_pid = getpid();
        child_req.request_id = (int)getpid() + i + 1000;
        snprintf(child_req.arg1, sizeof(child_req.arg1), "%s", label);
        snprintf(child_req.arg2, sizeof(child_req.arg2), "%s", position);
        snprintf(child_req.payload, sizeof(child_req.payload), "%s,%s", label, position);

        if (make_device_fifo_path(hub->children[i], child_fifo, sizeof(child_fifo)) != OK) {
            return ERR_IPC_FAILURE;
        }

        if (make_reply_fifo_path(getpid(), child_req.request_id, reply_fifo, sizeof(reply_fifo)) != OK) {
            return ERR_IPC_FAILURE;
        }

        rc = request_reply(child_fifo, reply_fifo, &child_req, &child_resp);
        if (rc != OK) {
            return rc;
        }

        if (child_resp.status != OK) {
            return child_resp.status;
        }
    }

    return OK;
}

static int hub_check_children_consistency(device *dev, bool *consistent_out)
{
    hub_device *hub = (hub_device *)dev;
    int rc;
    int i;
    state first_state = STATE_OFF;
    bool first_set = false;

    if (dev == NULL || consistent_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }


    if (hub->child_count == 0) {
        *consistent_out = true;
        return OK;
    }
    for (i = 0; i < hub->child_count; i++) {
        domo_message req;
        domo_message resp;
        char child_fifo[PATH_MAX];
        char reply_fifo[PATH_MAX];

        memset(&req, 0, sizeof(req));
        memset(&resp, 0, sizeof(resp));

        req.kind = MSG_REQUEST;
        snprintf(req.sender_id, sizeof(req.sender_id), "%d", dev->info.id);
        snprintf(req.command, sizeof(req.command), "%s", CMD_INFO);
        req.src_id = dev->info.id;
        req.dst_id = hub->children[i];
        req.target_id = hub->children[i];
        req.src_pid = getpid();
        req.request_id = (int)getpid() + i;
        snprintf(req.payload, sizeof(req.payload), "ALL");

        if (make_device_fifo_path(hub->children[i], child_fifo, sizeof(child_fifo)) != OK) {
            return ERR_IPC_FAILURE;
        }

        if (make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo)) != OK) {
            return ERR_IPC_FAILURE;
        }

        rc = request_reply(child_fifo, reply_fifo, &req, &resp);
        if (rc != OK) {
            return rc;
        }

        state child_state;

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

    dev->info.state = first_state;
    *consistent_out = true;
    return OK;
}

static int hub_build_info_payload(hub_device *hub, char *buf, size_t len) {
    bool consistent;
    int rc;

    if (hub == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    rc = hub_check_children_consistency(&hub->base, &consistent);
    if (rc != OK) {
        snprintf(buf, len, "hub id=%d state=%s error=consistency_check_failed",
                 hub->base.info.id, state_str(hub->base.info.state));
        return OK;
    }

    hub->manual_override = !consistent;

    if (hub->manual_override) {
        snprintf(buf, len, "hub id=%d state=manual_override", hub->base.info.id);
    } else {
        snprintf(buf, len, "hub id=%d state=%s",
                 hub->base.info.id, state_str(hub->base.info.state));
    }

    return OK;
}

static int hub_handle_message(device *dev,const domo_message *req,domo_message *resp) 
{
    hub_device *hub=(hub_device *)dev;

    if(hub==NULL || req==NULL || resp==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    memset(resp,0,sizeof(*resp));
    resp->kind=MSG_RESPONSE ;
    snprintf(resp->command,sizeof(resp->command),"%s",req->command);
    snprintf(resp->sender_id,sizeof(resp->sender_id),"%d",hub->base.info.id) ;
    resp->src_id=hub->base.info.id;
    resp->dst_id=req->src_id ;
    resp->src_pid=getpid();
    resp->request_id=req->request_id ;
    resp->status=OK;

    simulate_random_delay();

    if(strcmp(req->command,CMD_INFO)==0) {
        return hub_build_info_payload(hub,resp->payload,sizeof(resp->payload));
    }

    if (strcmp(req->command, CMD_SWITCH) == 0) {
        int rc;
        char label[32];
        char position[32];

        rc = parse_switch_args(req, label, sizeof(label), position, sizeof(position));
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "missing or invalid switch arguments");
            return OK;
        }

        if (strcmp(label, "power") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        if (strcmp(position, "on") != 0 && strcmp(position, "off") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        rc = hub_propagate_to_children(dev, req);
        if (rc != OK) {
            resp->status = rc;
            snprintf(resp->payload, sizeof(resp->payload), "failed to propagate switch to children");
            return OK;
        }

        hub->manual_override = false;

        if (strcmp(position, "on") == 0) {
            hub->base.info.state = STATE_ON;
        } else {
            hub->base.info.state = STATE_OFF;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                "hub %d switched %s", hub->base.info.id, state_str(hub->base.info.state));
        return OK;
    }

    if (strcmp(req->command, CMD_STATUS) == 0) {
        if (strncmp(req->payload, "manual_override,", 16) == 0) {
            hub->manual_override = true;
            return OK;
        }

        if (strncmp(req->payload, "child_added,", 12) == 0) {
            device_id child_id = (device_id)atoi(req->payload + 12);
            resp->status = hub_add_child(hub, child_id);
            return OK;
        }

        if (strncmp(req->payload, "child_removed,", 14) == 0) {
            device_id child_id = (device_id)atoi(req->payload + 14);
            int rc = hub_remove_child(hub, child_id);
            if (rc == ERR_DEVICE_NOT_FOUND) {
                rc = OK;
            }
            resp->status = rc;
            return OK;
        }

        return OK;
    }

    resp->status=ERR_INVALID_COMMAND ;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;
}

static int hub_init(device *dev) {
    hub_device *hub=(hub_device *)dev;
    hub->child_count = 0;

    if(hub==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    hub->manual_override=false;
    hub->base.info.state=STATE_OFF ;

    return OK;
}

static int hub_destroy(device *dev) {
    hub_device *hub=(hub_device *)dev;

    if(hub==NULL) {
        return ERR_INVALID_PARAMETERS ;
    }

    return OK;
}

int hub_device_main(device_id id) {
    hub_device hub;
    int fd,dummy_fd;
    int rc;

    memset(&hub,0,sizeof(hub));
    rc=device_common_init(&hub.base,id,DEVICE_HUB) ;
    if(rc!=OK) {
        return rc;
    }

    hub.base.handle_message=hub_handle_message;
    hub.base.destroy=hub_destroy;
    rc=hub_init(&hub.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_setup_fifo(&hub.base) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_open_fifo(&hub.base,&fd,&dummy_fd) ;
    if(rc!=OK) {
        return rc;
    }

    rc=device_common_main_loop(&hub.base,fd) ;
    device_common_cleanup(&hub.base,fd,dummy_fd);
    return rc;
}
