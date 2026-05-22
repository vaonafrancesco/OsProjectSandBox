#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"

// serialize_message
// converts a domo_message struct into a formatted protocol string.
// format: SENDER_ID|COMMAND|TARGET_ID|PAYLOAD

int serialize_message(const domo_message *msg, char *buffer, size_t max_len){
    if (msg == NULL || buffer == NULL) return IPC_ERROR;

    // Format the message into the required protocol string
	int len = snprintf(buffer, max_len, "%s|%s|%d|%s\n", msg->sender_id, msg->command, msg->target_id, msg->payload);
	
	// check if string is interrupted in the middle
	if(len<0 || (size_t)len >=max_len){
		return IPC_ERROR;
	}
	
	return OK;
}
// deserialize_message
//Parses a raw string buffer into a domo_message struct safely.

int deserialize_message(char *buffer, domo_message *msg){
    
    if (buffer == NULL || msg == NULL) return IPC_ERROR;
    
    //ensure no trailing newline interferes with parsing
	buffer[strcspn(buffer, "\n")] = '\0';
	
	// Parse the message using strtok_r (thread-safe version of strtok
	char *saveptr;
	char *sender = strtok_r(buffer, MSG_DELIMITER, &saveptr);
	char *cmd = strtok_r(NULL, MSG_DELIMITER, &saveptr);
	char *target = strtok_r(NULL, MSG_DELIMITER, &saveptr);
	char *payload = strtok_r(NULL, MSG_DELIMITER, &saveptr); //Payload can be NULL
	
	// validate that the mandatory fields are present
	if(!sender || !cmd || !target){
	return IPC_ERROR;
	}
	
	// Populate the domo_message structure safely
	strncpy(msg->sender_id, sender, sizeof(msg->sender_id) -1);
	msg->sender_id[sizeof(msg->sender_id)-1 ] = '\0';
	
	strncpy(msg->command, cmd, sizeof(msg->command) -1);
	msg->command[sizeof(msg->command)-1]= '\0';
	
	msg->target_id = atoi(target);
	
	//If there is a paylaod, copy it; if not, leave it empty
	if (payload != NULL){
		strncpy (msg->payload, payload, sizeof(msg->payload)-1);
		msg->payload[sizeof(msg->payload)-1] = '\0';
	}else {
		msg->payload[0] = '\0';
	}
	
	return OK;
}