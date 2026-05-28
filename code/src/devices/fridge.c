#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    device base;
    time_t last_state_change;
    int delay_seconds;
    time_t last_open_time;
    unsigned long total_open_time;
    
    int fill_percentage;
    int current_temp; 
    int thermostat_temp;
} fridge_device;

static const char *state_str(state state) 
{
    switch(state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static void update_open_time(fridge_device *fridge) {
    if(fridge == NULL) {
        return;
    }
    
    if(fridge->base.info.state == STATE_ON && fridge->last_open_time != 0) {
        time_t now = time(NULL);
        if(now > fridge->last_open_time) {
            fridge->total_open_time += (unsigned long)(now - fridge->last_open_time);
        }
        fridge->last_open_time = now;
    }
}

/**
 * 
 */
static int fridge_build_info_payload(const fridge_device *fridge, char *buf, size_t len) {
    if(fridge == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    update_open_time((fridge_device *)fridge);

    snprintf(buf, len,
             "fridge id=%d state=%s time=%lu delay=%d perc=%d temp=%d thermostat=%d",
             fridge->base.info.id, 
             state_str(fridge->base.info.state), 
             fridge->total_open_time,
             fridge->delay_seconds,
             fridge->fill_percentage,
             fridge->current_temp,
             fridge->thermostat_temp);
    return OK;
}

static int fridge_handle_message(device *dev, const domo_message *req, domo_message *resp) 
{
    fridge_device *fridge = (fridge_device *)dev;
    
    if (fridge == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(resp, 0, sizeof(*resp));
    resp->kind = MSG_RESPONSE;
    snprintf(resp->command, sizeof(resp->command), "%s", req->command);
    resp->src_id = fridge->base.info.id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = OK;

    simulate_random_delay();

    if(strcmp(req->command, CMD_INFO) == 0){
        return fridge_build_info_payload(fridge, resp->payload, sizeof(resp->payload));
    }

    if(strcmp(req->command, CMD_SWITCH) == 0){
        if(strcmp(req->arg1, "power") == 0) {
            update_open_time(fridge);

            if(strcmp(req->arg2, "on") == 0){
                fridge->base.info.state = STATE_ON;
                fridge->last_open_time = time(NULL);
            } else if(strcmp(req->arg2, "off") == 0) {
                fridge->base.info.state = STATE_OFF;
                fridge->last_open_time = 0;
            } else {
                resp->status = ERR_INVALID_PARAMETERS;
                snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
                return OK;
            }

            snprintf(resp->payload, sizeof(resp->payload),
                     "fridge %d switched %s", fridge->base.info.id, state_str(fridge->base.info.state));
            return OK;
        }

        if (strcmp(req->arg1, "temperature") == 0) {
            if(strcmp(req->arg2, "up") == 0) {
                fridge->thermostat_temp++;
            } 
            else if(strcmp(req->arg2, "down") == 0) {
                fridge->thermostat_temp--;
            } 
            else {
                resp->status = ERR_INVALID_PARAMETERS;
                snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
                return OK;
            }

            snprintf(resp->payload, sizeof(resp->payload),
                     "fridge %d thermostat set to %d", fridge->base.info.id, fridge->thermostat_temp);
            return OK;
        }

        resp->status = ERR_INVALID_PARAMETERS;
        snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
        return OK;
    }

    resp->status = ERR_INVALID_COMMAND;
    snprintf(resp->payload, sizeof(resp->payload), "unknown command");
    return OK;
}

static int fridge_init(device *dev) 
{
    fridge_device *fridge = (fridge_device *)dev;
    
    if(fridge == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    // initialize fridge state
    fridge->last_state_change = 0;
    fridge->last_open_time = 0;
    fridge->total_open_time = 0;
    fridge->delay_seconds = 30;
    fridge->fill_percentage = 50;
    fridge->current_temp = 4;    // 4°C default
    fridge->thermostat_temp = 4;
    
    return OK;
}

static int fridge_destroy(device *dev) {
    fridge_device *fridge = (fridge_device *)dev;
    
    if (fridge == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    update_open_time(fridge);  // final update
    
    return OK;
}

int fridge_device_main(device_id id) {
    fridge_device fridge;
    int fd, dummy_fd;
    int rc;

    memset(&fridge, 0, sizeof(fridge));

    rc = device_common_init(&fridge.base, id, DEVICE_FRIDGE);
    if(rc != OK) {
        return rc;
    }

    fridge.base.init = fridge_init;
    fridge.base.handle_message = fridge_handle_message;
    fridge.base.destroy = fridge_destroy;

    rc = fridge_init(&fridge.base);
    if (rc != OK) {
        return rc;
    }

    rc = device_common_setup_fifo(&fridge.base);
    if(rc != OK) {
        return rc;
    }

    rc = device_common_open_fifo(&fridge.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }

    rc = device_common_main_loop(&fridge.base, fd);

    device_common_cleanup(&fridge.base, fd, dummy_fd);

    return rc;
}