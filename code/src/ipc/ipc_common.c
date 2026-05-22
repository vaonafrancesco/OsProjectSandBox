#include <stdio.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"

//ipc_print_message
//Utility function to print a domo_message to the stdout. useful for debugging

void ipc_print_message(const domo_message *msg){
    if (msg==NULL){
        printf("[IPC DEBUG] Can't print: message is NULL.\n");
        return;
    }

    printf("[IPC DEBUG] Sender: %s | Cmd: %s | Target: %d | Payload: %s\n", msg->sender_id, msg->command, msg->target_id, (msg->payload[0] != '\0') ? msg->payload : "NULL");
}

//ipc_create_message
//Helper function to quickly populate a domo_message struct safely. Saves otehr modules from having to write multiple strncpy lines manually.

void ipc_create_message(domo_message *msg, const char *sender, const char *cmd, int target, const char *payload){
    if (msg == NULL) return;

    strncpy(msg->sender_id, sender, sizeof(msg->sender_id) -1);
	msg->sender_id[sizeof(msg->sender_id) -1]='\0';
	
	strncpy(msg->command, cmd, sizeof(msg->command) -1);
	msg->command[sizeof(msg->command) -1]='\0';
	
	msg->target_id = target;
	
	if (payload != NULL){
		strncpy(msg->payload, payload, sizeof(msg->payload) -1);
			msg->payload[sizeof(msg->payload) -1]='\0';
	}else{
		msg->payload[0] = '\0';
	}


}
