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

    int open_switch_state; //if 0 is off, if 1 is on
    int close_switch_state; //if 0 is off, if 1 is on
} window_device;

static int window_update(device *dev);

// Simple helper to convert the internal state enum into a readable string
static const char *state_str(state state) {
    if (state == STATE_OPEN){
        return "open";
    }else{
        return "closed";
    }
}

// Calculates the time the window has been left open and adds it to the total.
static void update_usage_time(window_device *window) 
{
    if (window== NULL) {
        return;
    }
    
    // We only count the time if the window is currently open
    if (window->base.info.state == STATE_OPEN && window->last_state_change != 0) {
        time_t now = time(NULL);
        if (now > window->last_state_change) {
            window->total_open_time += (unsigned long)(now - window->last_state_change);}
        
		// Reset the timestamp so we don't double-count this time block later
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

    snprintf(buf, len,
             "window id=%d parent=%d state=%s time=%lu open_switch=%d close_switch=%d",
             window->base.info.id, window->base.info.logical_parent_id, state_str(window->base.info.state), window->total_open_time,
             window->open_switch_state, window->close_switch_state);
    return OK;
}

// The core brain of the window device. Handles incoming IPC messages.
static int window_handle_message(device *dev, const domo_message *req, domo_message *resp) {
    if (dev == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
	
	// Double check that we are actually dealing with a window
    if (dev->info.type != DEVICE_WINDOW) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }
    
    // Make sure our timers are up to date before processing the message
    window_update(dev);
	
	// Cast the generic device pointer into our specific window struct
    window_device *window = (window_device *)dev;

	// Prepare the standard response headers
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

    // COMMAND: INFO
    if (strcmp(req->command, CMD_INFO) == 0) {
        return window_build_info_payload(window, resp->payload, sizeof(resp->payload));
    }
	
	// COMMAND: LINK
    if (strcmp(req->command, CMD_LINK) == 0) {
        int parent_id;
        // Parse the payload expecting the format "parent,<id>"
        int rc = sscanf(req->payload, "parent,%d", &parent_id);

        if(rc != 1 || parent_id < 0){
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid link payload");
            return OK;
        }

        window->base.info.logical_parent_id = parent_id;
        snprintf(resp->payload, sizeof(resp->payload),
                 "window %d linked to parent %d",
                 window->base.info.id,
                 parent_id);
        return OK;
    }

    // COMMAND: SWITCH
    if (strcmp(req->command, CMD_SWITCH) == 0) {
        
        // Strict validation for arg2: it must be exactly "on" or "off"
        if (strcmp(req->arg2, "on") != 0 && strcmp(req->arg2, "off") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        // Strict validation for arg1: accepts "open", "close" and the universal "sys_state" label
        if (strcmp(req->arg1, "open") != 0 && strcmp(req->arg1, "close") != 0 && strcmp(req->arg1, "sys_state") != 0) {
            resp->status=ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        update_usage_time(window);

        if (strcmp(req->arg2, "on") == 0) {
        	// "open on" or "sys_state on" means we open the window
            if(strcmp(req->arg1, "open") == 0 || strcmp(req->arg1, "sys_state") == 0) {
                window->base.info.state = STATE_OPEN;
                window->last_state_change = time(NULL); 
                window->open_switch_state = 1;
                window->close_switch_state = 0;
            } 
			// "close on" means we push the 'close' button 
			else if (strcmp(req->arg1, "close") == 0) {
                window->base.info.state = STATE_CLOSED;
                window->last_state_change = 0; 
                window->close_switch_state = 1;
                window->open_switch_state = 0;
            }
        } 
        else if (strcmp(req->arg2, "off") == 0) {
            // A "sys_state off" or "close off" forcefully closes the window
            if(strcmp(req->arg1, "sys_state") == 0 || strcmp(req->arg1, "close") == 0) {
                window->base.info.state = STATE_CLOSED;
                window->last_state_change = 0; 
                window->close_switch_state = 1;
                window->open_switch_state = 0;
            }
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "window %d switched %s", window->base.info.id, state_str(window->base.info.state));
        return OK;
    }

	// If we receive an unknown command, let the sender know
    resp->status = ERR_INVALID_COMMAND;
    snprintf(resp->payload,sizeof(resp->payload),"unknown command");
    return OK;

}

// Set up the default variables when the window is first created
static int window_init(device *dev){

    if (dev->info.type != DEVICE_WINDOW) { 
    return ERR_DEVICE_TYPE_MISMATCH; 
    }
    window_device  *window =(window_device *)dev;

    if(window==NULL){
        return ERR_INVALID_PARAMETERS;    }
	
	// Default state: closed, no parent, timers at zero
    window->base.info.state = STATE_CLOSED;
    window->base.info.logical_parent_id=0;
    window-> last_state_change=0;
    window->total_open_time=0 ;
    window->open_switch_state = 0;
    window->close_switch_state = 1;

    return OK;

}

// Cleanup function right before the process terminates
static int window_destroy(device *dev){
    window_device *window =(window_device*)dev;

    if (window==NULL)
    {
        return ERR_INVALID_PARAMETERS;
    }
	
	// Save the final chunk of time before we die
    update_usage_time(window);
    return OK;
    
}

// Hook called continuously by the select() loop in device_common_main_loop
static int window_update(device *dev) {
    window_device *window = (window_device *)dev;
    
    if (window == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
    
    // Update open time counter periodically
    update_usage_time(window);
    
    return OK;
}

// entry point for the window process
int window_device_main(device_id id){
    window_device window;
    int fd, dummy_fd ;
    int rc;

    memset(&window,0,sizeof(window));
    
    // Initialize the base device fields
    rc = device_common_init(&window.base, id, DEVICE_WINDOW);

        if (rc != OK) {
            return rc;
        }
        
        // Bind our custom functions to the base struct's function pointers
        window.base.handle_message= window_handle_message;
        window.base.destroy=window_destroy;
        window.base.update=window_update;

        // Initialize custom variables
        rc= window_init(&window.base) ;
        
        if (rc != OK) {
            return rc;
        }
		// Create the FIFO file on disk and set up signals
		rc=device_common_setup_fifo(&window.base);
        if (rc!=OK) {
        return rc ; 
    }
    
    // Open the FIFO so we can start reading commands
    rc = device_common_open_fifo(&window.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }
    
    // Enter the infinite listening loop
    rc =device_common_main_loop(&window.base, fd);
    
    // Once the loop breaks, clean up resources
    device_common_cleanup(&window.base, fd, dummy_fd);
    return rc;}

