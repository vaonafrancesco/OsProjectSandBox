#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"

//ipc_recv_message
//reads a raw string from the FIFO and parses it into the domo_message struct
// format: SENDER_ID|COMMAND|TARGET_ID|PAYLOAD

int ipc_recv_message(int fd_in, domo_message *msg){
    if (fd_in <0 || msg == NULL) return IPC_ERROR;

    char buffer[MAX_MSG_LEN];
	memset(buffer, 0, sizeof(buffer));
	
	// REad the incoming string from the FIFO
	ssize_t bytes_read = read(fd_in, buffer, sizeof(buffer)-1);
	if (bytes_read <=0){
		return IPC_ERROR; //error or empty read
	}
	
	
	//ensure null-termination and remove the trailing newline character
	buffer[bytes_read] = '\0';
	buffer[strcspn(buffer,"\n")] = '\0';
	
	//Parse the message using strtok_r (thread safe version of strtok)
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

//ipc_send_message
//Serializes the domo_message struct into a string and sends it.
//Uses O_NONBLOCK to prevent the process from hanging if the target device crashed and is not reading FIFO.

int ipc_send_message(const domo_message *msg){
    if(msg == NULL) return IPC_ERROR;

    char fifo_path[256];
	snprintf(fifo_path, sizeof(fifo_path), "%s%d", FIFO_PATH_PREFIX, msg->target_id);
	
	//Open the target FIFO for writing
	//If the reader (target process) does not exist, open() will fail with ENXIO insted of blocking our process forever.
	int fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
	if(fd_out< 0){
		if(errno == ENXIO){
			//No reader on other side
			return DEVICE_NOT_FOUND;
		}
		return IPC_ERROR;
	}
	
	//format the message into the required protocol string
	char buffer[MAX_MSG_LEN];
	int len = snprintf(buffer, sizeof(buffer), "%s|%s|%d|%s\n", msg->sender_id, msg->command, msg->target_id, msg->payload);
	
	// check if string is interrupted in the middle
	if(len>=(int)sizeof(buffer)){
		close(fd_out);
		return IPC_ERROR;
	}
	
	//write the formatted string to the FIFO
	ssize_t written = write(fd_out, buffer, len);
	close(fd_out); //Always cose the write end when done
	
	if(written != len){
	return IPC_ERROR;
	}
	
	return OK;
	
}
