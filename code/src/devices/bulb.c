#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "utils.h"

typedef struct {
    device base;
    time_t last_state_change;
    unsigned long total_usage_time;
} bulb_device;

// Simple helper to convert the state enum into a printable string
static const char *state_str(state state)
{
    switch (state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

/*	Calculates how much time has passed since the bulb was turned on,
	adds it to the total, and resets the timer. */
static void update_usage_time(bulb_device *bulb)
{
    if (bulb == NULL) {
        return;
    }
	
	// We only care about adding time if the bulb is actually ON
    if (bulb->base.info.state == STATE_ON && bulb->last_state_change != 0) {
        time_t now = time(NULL);
        if (now > bulb->last_state_change) {
            bulb->total_usage_time += (unsigned long)(now - bulb->last_state_change);
        }
        bulb->last_state_change = now;	// Reset the clock for the next check
    }
}

// Builds the string containing all the bulb's stats to send back when requested
static int bulb_build_info_payload(bulb_device *bulb, char *buf, size_t len)
{
    if (bulb == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

	// Make sure the timer is up to date before printing it
    update_usage_time(bulb);

    snprintf(buf, len,
             "bulb id=%d parent=%d state=%s manual_override=%s time=%lu",
             bulb->base.info.id,
             bulb->base.info.logical_parent_id,
             state_str(bulb->base.info.state),
             bulb->base.info.manual_override ? "true" : "false",
             bulb->total_usage_time);
    return OK;
}

// Extracts the target "label" and "position"  from a switch command
static int parse_switch_args(const domo_message *req,
                             char *label, size_t label_len,
                             char *position, size_t position_len)
{
    char local[MAX_MSG_LEN];
    char *comma;

    if (req == NULL || label == NULL || position == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    label[0] = '\0';
    position[0] = '\0';

	// First, check if the arguments were sent cleanly in arg1 and arg2
    if (req->arg1[0] != '\0' && req->arg2[0] != '\0') {
        snprintf(label, label_len, "%s", req->arg1);
        snprintf(position, position_len, "%s", req->arg2);
        return OK;
    }

	// If not, maybe they are packed together in the payload string
    if (req->payload[0] == '\0') {
        return ERR_INVALID_PARAMETERS;
    }

    snprintf(local, sizeof(local), "%s", req->payload);
    comma = strchr(local, ',');	// Find the comma separator
    if (comma == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    *comma = '\0';	// Split the string into two pieces
    snprintf(label, label_len, "%s", local);
    snprintf(position, position_len, "%s", comma + 1);
    return OK;
}

// Reads the new parent ID from the payload string of a LINK command
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

// Receives a message, figures out what to do and prepares a response message.
static int bulb_handle_message(device *dev, const domo_message *req, domo_message *resp)
{
	// Cast the generic device pointer back into our specific bulb_device struct
    bulb_device *bulb = (bulb_device *)dev;

    if (bulb == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
	
	// Prepare the standard headers for the response message
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
	
	// COMMAND: INFO
    if (strcmp(req->command, CMD_INFO) == 0) {
        return bulb_build_info_payload(bulb, resp->payload, sizeof(resp->payload));
    }

	// COMMAND: LINK
    if (strcmp(req->command, CMD_LINK) == 0) {
        int parent_id;
        int rc = parse_link_parent_id(req, &parent_id);

        if (rc != OK) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid link payload");
            return OK;
        }

        bulb->base.info.logical_parent_id = parent_id;
        snprintf(resp->payload, sizeof(resp->payload),
                 "bulb %d linked to parent %d",
                 bulb->base.info.id,
                 parent_id);
        return OK;
    }

	// COMMAND: SWITCH
    if (strcmp(req->command, CMD_SWITCH) == 0) {
    	//	If the message comes from outside the system or has no valid source we assume manual override.
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

		// Bulbs only understand "power" or "sys_state"
        if (strcmp(label, "power") != 0 && strcmp(label, "sys_state") != 0) {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch label");
            return OK;
        }

        update_usage_time(bulb);	// Update timer BEFORE changing the state

        if (strcmp(position, "on") == 0) {
            bulb->base.info.state = STATE_ON;
            bulb->base.info.manual_override = is_manual_override;
            bulb->last_state_change = time(NULL);	// Start the clock
        } else if (strcmp(position, "off") == 0) {
            bulb->base.info.state = STATE_OFF;
            bulb->base.info.manual_override = is_manual_override;
            bulb->last_state_change = 0;	// Stop the clock
        } else {
            resp->status = ERR_INVALID_PARAMETERS;
            snprintf(resp->payload, sizeof(resp->payload), "invalid switch position");
            return OK;
        }

        snprintf(resp->payload, sizeof(resp->payload),
                 "bulb %d switched %s",
                 bulb->base.info.id,
                 state_str(bulb->base.info.state));
        return OK;
    }
    
	// If we reach here, we received a command we don't understand
    resp->status = ERR_INVALID_COMMAND;
    snprintf(resp->payload, sizeof(resp->payload), "unknown command");
    return OK;
}

// Basic initialization for the bulb's custom variables
static int bulb_init(device *dev)
{
    bulb_device *bulb = (bulb_device *)dev;

    if (bulb == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
    bulb->base.info.logical_parent_id=0;
    bulb->last_state_change = 0;
    bulb->total_usage_time = 0;
    return OK;
}

// Cleanup function called right before the process dies
static int bulb_destroy(device *dev)
{
    bulb_device *bulb = (bulb_device *)dev;

    if (bulb == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    update_usage_time(bulb);	// Save final time
    return OK;	
}

// entry point for the bulb proces
int bulb_device_main(device_id id)
{
    bulb_device bulb;
    int fd, dummy_fd;
    int rc;

	// Set up the generic device parts
    memset(&bulb, 0, sizeof(bulb));
    rc = device_common_init(&bulb.base, id, DEVICE_BULB);
    if (rc != OK) {
        return rc;
    }

	// assign our bulb-specific functions to the base struct's pointers
    bulb.base.handle_message = bulb_handle_message;
    bulb.base.destroy = bulb_destroy;

	// Initialize our specific bulb variables (timers)
    rc = bulb_init(&bulb.base);
    if (rc != OK) {
        return rc;
    }
	
	// Create the FIFO file and hook up the signal handler
    rc = device_common_setup_fifo(&bulb.base);
    if (rc != OK) {
        return rc;
    }

	// Open the FIFO for reading
    rc = device_common_open_fifo(&bulb.base, &fd, &dummy_fd);
    if (rc != OK) {
        return rc;
    }

	// Jump into the infinite select() loop to wait for messages
    rc = device_common_main_loop(&bulb.base, fd);
    
    // When the loop breaks, clean up files and memory
    device_common_cleanup(&bulb.base, fd, dummy_fd);
    return rc;
}
