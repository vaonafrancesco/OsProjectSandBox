#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

void ipc_create_message(domo_message *msg,
                        const char *sender,
                        const char *cmd,
                        int target,
                        const char *payload)
{
	// Safety first: if the pointer is NULL, doing anything else will crash the program.
    if (msg == NULL) {
        return;
    }

	// Zero out the entire struct memory. 
    /*	This is a great practice to make sure no "garbage" data is left inside
    	from whatever was in that memory location before. */
    memset(msg, 0, sizeof(*msg));
	
	// Safely copy the sender string.
    if (sender != NULL) {
   		// We use strncpy instead of strcpy to prevent buffer overflows if the string is too long.
        strncpy(msg->sender_id, sender, sizeof(msg->sender_id) - 1);
        // Manually force the null-terminator at the end, just to be 100% safe!
        msg->sender_id[sizeof(msg->sender_id) - 1] = '\0';
    } else {
        msg->sender_id[0] = '\0';
    }
    
	// Safely copy the command string
    if (cmd != NULL) {
        strncpy(msg->command, cmd, sizeof(msg->command) - 1);
        msg->command[sizeof(msg->command) - 1] = '\0';
    } else {
        msg->command[0] = '\0';
    }

	// Set up the default routing and tracking variables
    msg->target_id = target;
    msg->src_id = -1;
    msg->dst_id = target;
    msg->src_pid = getpid();
    msg->request_id = 0;
    msg->status = OK;
    msg->kind = MSG_REQUEST;
	
	// Clear out the extra arguments just in case
    msg->arg1[0] = '\0';
    msg->arg2[0] = '\0';

	// Safely copy the extra payload data
    if (payload != NULL) {
        strncpy(msg->payload, payload, sizeof(msg->payload) - 1);
        msg->payload[sizeof(msg->payload) - 1] = '\0';
    } else {
        msg->payload[0] = '\0';
    }
}
