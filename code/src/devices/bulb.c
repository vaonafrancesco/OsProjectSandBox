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
        return ERR_INVALID_PARAMETERS;}
    update_usage_time(bulb)  ;

    snprintf(buf, len,
             "bulb id=%d state=%s time=%lu",
             bulb->base.info.id, state_str(bulb->base.info.state), bulb->total_usage_time);
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
            if (strcmp(req->arg1, "power") != 0) {
                resp->status = ERR_INVALID_PARAMETERS;
                snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
                return OK;
            }

            update_usage_time(bulb);

            if (strcmp(req->arg2, "on") == 0)
            {
                bulb->base.info.state = STATE_ON;
                bulb->last_state_change = time(NULL);
            }else if (strcmp(req->arg2, "off") == 0) {
                bulb->base.info.state = STATE_OFF ;
                bulb->last_state_change = 0 ;
            }else{
                resp->status = ERR_INVALID_PARAMETERS;
                snprintf( resp->payload, sizeof(resp->payload), "invalid switch position");
                return OK ;

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

    memset(&bulb, 0, sizeof(bulb));
    rc = device_common_init(&bulb.base, id, DEVICE_BULB);
    if (rc != OK) {
        return rc;
    }

    bulb.base.handle_message = bulb_handle_message;
    bulb.base.destroy = bulb_destroy;
    rc = bulb_init(&bulb.base);
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
    rc = device_common_main_loop(&bulb.base, fd);
    device_common_cleanup(&bulb.base, fd, dummy_fd);
    return rc;
}
