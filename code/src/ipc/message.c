#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"


//ipc_recv_message
//reads a raw string from the FIFO and parses it into the domo_message struct
// format: SENDER_ID|COMMAND|TARGET_ID|PAYLOAD

int ipc_recv_message(int fd_in, domo_message *msg){
    if (fd_in <0 || msg == NULL) return ERR_IPC_FAILURE;

    memset(msg, 0, sizeof(*msg)); //--modifica aggiunto

    char buffer[MAX_MSG_LEN];
	memset(buffer, 0, sizeof(buffer));
	
// Read the incoming string from the FIFO
    ssize_t bytes_read = read(fd_in, buffer, sizeof(buffer)-1);
    if (bytes_read <= 0) {
        return ERR_IPC_FAILURE; // error or empty read
    }

    // ensure null-termination and remove the trailing newline character
    buffer[bytes_read] = '\0';
    buffer[strcspn(buffer, "\n")] = '\0';

    // Parse the message fields explicitly to preserve empty tokens.
    char *fields[11];
    char *iter = buffer;
    for (int i = 0; i < 10; i++) {
        char *delim = strchr(iter, MSG_DELIMITER_CHAR);
        if (delim == NULL) {
            return ERR_IPC_FAILURE;
        }
        fields[i] = iter;
        *delim = '\0';
        iter = delim + 1;
    }
    fields[10] = iter; // payload may contain delimiters

    char *sender = fields[0];
    char *cmd = fields[1];
    char *target = fields[2];
    char *src_id = fields[3];
    char *dst_id = fields[4];
    char *src_pid = fields[5];
    char *request_id = fields[6];
    char *arg1 = fields[7];
    char *arg2 = fields[8];
    char *status = fields[9];
    char *payload = fields[10];

    // validate that the mandatory fields are present
    if (!sender || !cmd || !target || !src_id || !dst_id || !src_pid || !request_id || !arg1 || !arg2 || !status) {
        return ERR_IPC_FAILURE;
    }

    // Populate the domo_message structure safely
    strncpy(msg->sender_id, sender, sizeof(msg->sender_id) -1);
    msg->sender_id[sizeof(msg->sender_id)-1 ] = '\0';
    strncpy(msg->command, cmd, sizeof(msg->command) -1);
    msg->command[sizeof(msg->command)-1] = '\0';

    msg->target_id = atoi(target);
    msg->src_id = atoi(src_id);
    msg->dst_id = atoi(dst_id);
    msg->src_pid = (pid_t)atoi(src_pid);
    msg->request_id = atoi(request_id);
    strncpy(msg->arg1, arg1, sizeof(msg->arg1) -1);
    msg->arg1[sizeof(msg->arg1)-1] = '\0';
    strncpy(msg->arg2, arg2, sizeof(msg->arg2) -1);
    msg->arg2[sizeof(msg->arg2)-1] = '\0';
    msg->status = atoi(status);

    //If there is a payload, copy it; if not, leave it empty
    if (payload != NULL){
        strncpy(msg->payload, payload, sizeof(msg->payload)-1);
        msg->payload[sizeof(msg->payload)-1] = '\0';
    } else {
        msg->payload[0] = '\0';
    }
    
    return OK;
}

//ipc_send_message
//Serializes the domo_message struct into a string and sends it.
//Uses O_NONBLOCK to prevent the process from hanging if the target device crashed and is not reading FIFO.

int ipc_send_message(const domo_message *msg){
    if(msg == NULL) return ERR_IPC_FAILURE;

    char fifo_path[256];
	snprintf(fifo_path, sizeof(fifo_path), "%s%d", FIFO_PATH_PREFIX, msg->target_id);
	
	//Open the target FIFO for writing
	//If the reader (target process) does not exist, open() will fail with ENXIO insted of blocking our process forever.
	int fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
	if (fd_out < 0) {
        return ERR_IPC_FAILURE;
    }

	//format the message into the required protocol string
	char buffer[MAX_MSG_LEN];
	int len = snprintf(buffer, sizeof(buffer), "%s|%s|%d|%d|%d|%d|%d|%s|%s|%d|%s\n",
	                   msg->sender_id,
	                   msg->command,
	                   msg->target_id,
	                   msg->src_id,
	                   msg->dst_id,
	                   (int)msg->src_pid,
	                   msg->request_id,
	                   msg->arg1,
	                   msg->arg2,
	                   msg->status,
	                   msg->payload);


	// check if string is interrupted in the middle
	if(len>=(int)sizeof(buffer)){
		close(fd_out);
		return ERR_IPC_FAILURE;
	}
	
	//write the formatted string to the FIFO
    ssize_t written = write(fd_out, buffer, len);
    close(fd_out);
	if(written != len){
		return ERR_IPC_FAILURE;
	}
	
	return OK;
	
}
