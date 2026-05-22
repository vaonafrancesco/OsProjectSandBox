#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "../../include/protocol.h"
#include "../../include/routing.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

//Global routing table
//declared here but accessed by hierarchy.c via 'extern'
routing_node routing_table[MAX_DEVICES];

//routing_init
//initializes the routing table with all the slots set to empty (-1)

void routing_init(void){
    for (int i=0; i<MAX_DEVICES; i++){
    routing_table[i].id = -1; // -1 =empty slot
    routing_table[i].parent_id = -1;
    routing_table[i].type = DEVICE_UNKNOWN;
    }
}

//routing_add_node
//Register a new device in the system with its parent as default setted to 0.

int routing_add_node(int id, device_type type){
    if (id<0)return ERR_INVALID_PARAMETERS;

        for (int i = 0; i< MAX_DEVICES; i++){
		if (routing_table[i].id == -1){
			routing_table[i].id = id;
			routing_table[i].parent_id = CONTROLLER_ID; // Always defaults to 0
			routing_table[i].type = type;
			return OK;
		}
	}
	return ERR_INVALID_PARAMETERS; // Routing table is full
}

//routing_remove_node
//removes a device from the routing table (cascading deletion of children is handled at the IPC/CONTROLLER level.

int routing_remove_node(int id){
    int found = 0;

        for (int i =0; i< MAX_DEVICES; i++){
	    	if (routing_table[i].id == id){
		    	routing_table[i].id = -1;	//Mark slots as deleted
			    routing_table[i].parent_id = -1;
			    routing_table[i].type = DEVICE_UNKNOWN;
		    	found = 1;
			    break;
		    }
	    }
	
	return found ? OK : ERR_DEVICE_NOT_FOUND;
}


// Helper function to create reply FIFO path
int make_reply_fifo_path(pid_t pid, int request_id, char *path, size_t path_len) {
    if (path == NULL || path_len == 0) {
        return ERR_INVALID_PARAMETERS;
    }
    snprintf(path, path_len, "%s%d_%d.fifo", FIFO_PATH_PREFIX, (int)pid, request_id);
    return OK;
}

// Helper function for request-reply pattern
int request_reply(const char *target_fifo, const char *reply_fifo,
                  const domo_message *request, domo_message *response) {
    int fd_reply;
    int rc;

    if (target_fifo == NULL || reply_fifo == NULL || request == NULL || response == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    // Create reply FIFO
    unlink(reply_fifo);
    if (mkfifo(reply_fifo, 0666) != 0 && errno != EEXIST) {
        return ERR_SYSTEM;
    }

    // Open reply FIFO for reading (non-blocking first)
    fd_reply = open(reply_fifo, O_RDONLY | O_NONBLOCK);
    if (fd_reply < 0) {
        unlink(reply_fifo);
        return ERR_SYSTEM;
    }

    // Send request to target
    rc = ipc_send_message(request);
    if (rc != OK) {
        close(fd_reply);
        unlink(reply_fifo);
        return rc;
    }

    // Wait for response with timeout
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd_reply, &read_fds);

    struct timeval tv;
    tv.tv_sec = TIMEOUT_DEVICE;
    tv.tv_usec = 0;

    int retval = select(fd_reply + 1, &read_fds, NULL, NULL, &tv);
    if (retval == -1) {
        close(fd_reply);
        unlink(reply_fifo);
        return ERR_IPC_FAILURE;
    } else if (retval == 0) {
        close(fd_reply);
        unlink(reply_fifo);
        return ERR_TIMEOUT;
    }

    // Read response
    rc = ipc_recv_message(fd_reply, response);
    close(fd_reply);
    unlink(reply_fifo);

    return rc;
}

// Helper function to send message to a specific FIFO path
int send_message_to_fifo(const char *fifo_path, const domo_message *msg) {
    int fd_out;

    if (fifo_path == NULL || msg == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_out < 0) {
        if (errno == ENXIO) {
            return ERR_DEVICE_NOT_FOUND;
        }
        return ERR_IPC_FAILURE;
    }

    char buffer[MAX_MSG_LEN];
    int len = snprintf(buffer, sizeof(buffer), "%s|%s|%d|%s\n",
                       msg->sender_id, msg->command, msg->target_id, msg->payload);

    if (len >= (int)sizeof(buffer)) {
        close(fd_out);
        return ERR_IPC_FAILURE;
    }

    ssize_t written = write(fd_out, buffer, len);
    close(fd_out);

    if (written != len) {
        return ERR_IPC_FAILURE;
    }

    return OK;
}

// Helper function to create device FIFO path
int make_device_fifo_path(device_id id, char *path, size_t path_len) {
    if (path == NULL || path_len == 0) {
        return ERR_INVALID_PARAMETERS;
    }
    snprintf(path, path_len, "%s%d", FIFO_PATH_PREFIX, id);
    return OK;
}

