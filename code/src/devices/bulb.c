#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "device.h"
#include <unistd.h>


#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"


typedef struct {
    device base;
    time_t last_state_change;
    unsigned long total_usage_time;
} bulb_device;

static const char *state_str(state state) {
    switch (state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}


static void update_usage_time(bulb_device *bulb) 
{
    if (bulb== NULL) {
        return;
    }
    
    if (bulb->base.info.state == STATE_ON && bulb->last_state_change != 0) {
        time_t now = time(NULL);
        if (now > bulb->last_state_change) {
            bulb->total_usage_time += (unsigned long)(now - bulb->last_state_change);}
        bulb->last_state_change = now;
    }
}

static int bulb_build_info_payload(bulb_device *bulb, char *buf, size_t len) {
    if (bulb == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    update_usage_time(bulb);

    snprintf(buf, len,
             "bulb id=%d state=%s manual_override=%s time=%lu",
             bulb->base.info.id,
             state_str(bulb->base.info.state),
             bulb->base.info.manual_override ? "true" : "false",
             bulb->total_usage_time);
    return OK;
}

static int parse_switch_args(const domo_message *req,
                             char *label, size_t label_len,
                             char *position, size_t position_len) {
    char local[MAX_MSG_LEN];
    char *comma;

    if (req == NULL || label == NULL || position == NULL) {
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
    return OK;
}

static int bulb_handle_message(device *dev, const domo_message *req, domo_message *resp) {
    
    
    bulb_device *bulb = (bulb_device *)dev;


    if (bulb == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(resp, 0, sizeof(*resp));
    resp->kind = MSG_RESPONSE;
    snprintf(resp->command, sizeof(resp->command), "%s", req->command);
    snprintf(resp->sender_id, sizeof(resp->sender_id), "%d", bulb->base.info.id);

    resp->src_id = bulb->base.info.id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = OK;

    simulate_random_delay();

    if (strcmp(req->command, CMD_INFO) == 0) {
        return bulb_build_info_payload(bulb, resp->payload, sizeof(resp->payload));
    }
    if (strcmp(req->command, CMD_SWITCH) == 0) {
        bool is_manual_override = (strcmp(req->sender_id, EXT_SENDER_ID) == 0) || (req->src_id == -1);
        int rc;
        char label[64];
        char position[64];

        rc = parse_switch_args(req, label, sizeof(label), position, sizeof(position));
        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch payload");
            return OK;
        }

        if (strcmp(label, "power") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        update_usage_time(bulb);

        if (strcmp(position, "on") == 0) {
            bulb->base.info.state = STATE_ON;
            bulb->base.info.manual_override = is_manual_override;
            bulb->last_state_change = time(NULL);
        } else if (strcmp(position, "off") == 0) {
            bulb->base.info.state = STATE_OFF;
            bulb->base.info.manual_override = is_manual_override;
            bulb->last_state_change = 0;
        } else {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                "bulb %d switched %s", bulb->base.info.id, state_str(bulb->base.info.state));
        return OK;
    }


            resp->status = ERR_INVALID_COMMAND;
            snprintf(resp->payload, sizeof(resp->payload), "unknown command");
            return OK;
}


static int bulb_init(device *dev){

    bulb_device  *bulb =(bulb_device *)dev;

    if(bulb==NULL){
        return ERR_INVALID_PARAMETERS;    }

    bulb-> last_state_change=0;
    bulb->total_usage_time=0 ;

    return OK;

}

static int bulb_destroy(device *dev){
    bulb_device *bulb =(bulb_device*)dev;

    if (bulb==NULL)
    {
        return ERR_INVALID_PARAMETERS;
    }

    update_usage_time(bulb);
    return OK;
    
}


int bulb_device_main(device_id id){
    bulb_device bulb;
    int fd, dummy_fd ;
    int rc;

    memset(&bulb,0,sizeof(bulb));
    rc = device_common_init(&bulb.base, id, DEVICE_BULB);

    if (rc != OK) {
        return rc;
    }
        
    bulb.base.handle_message= bulb_handle_message;
    bulb.base.destroy=bulb_destroy;
    rc= bulb_init(&bulb.base) ;
        
    if (rc != OK) {
        return rc;
    }
    rc = device_common_setup_fifo(&bulb.base);
    if (rc != OK) {
        return rc;
    }
    
    rc = device_common_open_fifo(&bulb.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }
    rc =device_common_main_loop(&bulb.base, fd);
    device_common_cleanup(&bulb.base, fd, dummy_fd);
    return rc;}