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
    unsigned long total_open_time;
    int open_switch_state;  // 0 = off, 1 = on
    int close_switch_state; // 0 = off, 1 = on
} window_device;

static const char *state_str(state state) {
    switch (state) {
        case STATE_OPEN: return "open";
        case STATE_CLOSED: return "closed";
        default: return "unknown";
    }
}


static void update_usage_time(window_device *window) 
{
    if (window== NULL) {
        return;
    }
    
    if (window->base.info.state == STATE_OPEN && window->last_state_change != 0) {
        time_t now = time(NULL);
        if (now > window->last_state_change) {
            window->total_open_time += (unsigned long)(now - window->last_state_change);}
        window->last_state_change = now;
    }
}


/**
 * method to build the response when "info" are asked
 */
static int window_build_info_payload(window_device *window, char *buf, size_t len) {
    if (window == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;}
    update_usage_time(window)  ;
    //update_usage_time((window_device *)window)  ;

    snprintf(buf, len,
             "window id=%d state=%s time=%lu open_switch=%d close_switch=%d",
             window->base.info.id, state_str(window->base.info.state), window->total_open_time,
             window->open_switch_state, window->close_switch_state);
    return OK;
}

static int window_handle_message(device *dev, const domo_message *req, domo_message *resp) {
    if (dev == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (dev->info.type != DEVICE_WINDOW) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    window_device *window = (window_device *)dev;


    memset(resp, 0, sizeof(*resp));
    resp->kind = MSG_RESPONSE;
    snprintf(resp->command, sizeof(resp->command), "%s", req->command);
    snprintf(resp->sender_id, sizeof(resp->sender_id), "%d", window->base.info.id);
    resp->src_id = window->base.info.id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = OK;

    simulate_random_delay();

    //if I am asking info about a window
    if (strcmp(req->command, CMD_INFO) == 0) {
        return window_build_info_payload(window, resp->payload, sizeof(resp->payload));
    }
    //if I am asking a switch(open/close)
    if (strcmp(req->command, CMD_SWITCH) == 0) {
        //Ci sono 2 ettichette open and close. Entrambe sono sempre a off. Quando viene chiamato "switch 2 open on" 
        //lo stato diventa open


        // VIt has to be on or off
        if (strcmp(req->arg2, "on") != 0 && strcmp(req->arg2, "off") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        // it has to be "open" or "close"
        if (strcmp(req->arg1, "open") != 0 && strcmp(req->arg1, "close") != 0) {
            resp->status=ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        update_usage_time(window);

        if (strcmp(req->arg2, "on") == 0)
        {
            if(strcmp(req->arg1, "open") == 0) {
                window->base.info.state = STATE_OPEN;
                window->last_state_change = time(NULL);
                window->open_switch_state = 1;  // Switch on
                // Auto-reset switch to off after triggering
                window->open_switch_state = 0;
            }else if (strcmp(req->arg1, "close") == 0) {
                window->base.info.state = STATE_CLOSED;
                window->last_state_change = 0;
                window->close_switch_state = 1; // Switch on
                // Auto-reset switch to off after triggering
                window->close_switch_state = 0;
            }
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "window %d switched %s", window->base.info.id, state_str(window->base.info.state));
        return OK;
    }


    resp->status = ERR_INVALID_COMMAND;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;

}


static int window_init(device *dev){

    if (dev->info.type != DEVICE_WINDOW) {
    return ERR_DEVICE_TYPE_MISMATCH;
    }
    window_device  *window =(window_device *)dev;

    if(window==NULL){
        return ERR_INVALID_PARAMETERS;    }

    window-> last_state_change=0;
    window->total_open_time=0 ;
    window->open_switch_state = 0;
    window->close_switch_state = 0;

    return OK;

}

static int window_destroy(device *dev){
    window_device *window =(window_device*)dev;

    if (window==NULL)
    {
        return ERR_INVALID_PARAMETERS;
    }

    update_usage_time(window);
    return OK;
    
}

static int window_update(device *dev) {
    window_device *window = (window_device *)dev;
    
    if (window == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
    
    // Update open time counter
    update_usage_time(window);
    
    return OK;
}


int window_device_main(device_id id){
    window_device window;
    int fd, dummy_fd ;
    int rc;

    memset(&window,0,sizeof(window));
    rc = device_common_init(&window.base, id, DEVICE_WINDOW);

        if (rc != OK) {
            return rc;
        }
        
        
        window.base.handle_message= window_handle_message;
        window.base.destroy=window_destroy;
        window.base.update=window_update;

        
        rc= window_init(&window.base) ;
        
        if (rc != OK) {
            return rc;
        }rc=device_common_setup_fifo(&window.base);
        if (rc!=OK) {
        return rc ; 
    }
    rc = device_common_open_fifo(&window.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }
    rc =device_common_main_loop(&window.base, fd);
    device_common_cleanup(&window.base, fd, dummy_fd);
    return rc;}
