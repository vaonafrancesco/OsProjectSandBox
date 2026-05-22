#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"

//ipc_send_request_and_wait
//sends a request message and waits for a reply on the provided fd_in.
// Uses select() to implement a timeout, preventing the process from blocking if the target device is unresponsive or dead.

int ipc_send_request_and_wait(const domo_message *request, domo_message *response, int fd_in){

    if(request == NULL || response == NULL || fd_in <0){
		return IPC_ERROR;
	}
	
	// Send the request message
	int send_status = ipc_send_message(request);
	if(send_status != OK){
		return send_status;		//return DEVICE_NOT_FOUND or IPC_ERROR
	}
	
	// Prepare the file descriptor set for select()
	fd_set read_fds;
	FD_ZERO(&read_fds);
	FD_SET(fd_in, &read_fds);
	
	
	// Set the timeout duration
	struct timeval tv;
	tv.tv_sec = TIMEOUT_DEVICE;
	tv.tv_usec = 0;
	
	//Wait for incoming data or timeout
	//select() return -1 or error, 0 on timeout, >0 if data is ready
	int retval = select(fd_in +1, &read_fds, NULL, NULL, &tv);
	
	if (retval == -1){
		perror("Error in select() during request-reply");
		return IPC_ERROR;
	} else if(retval == 0){
		//timeout exired, no response received
		printf("[IPC] Error: Timeout exired waiting for device %d to reply. \n", request->target_id);
		return IPC_ERROR;
	}
	
	// Data is ready to be read from the FIFO
	return ipc_recv_message(fd_in, response);
	
}
