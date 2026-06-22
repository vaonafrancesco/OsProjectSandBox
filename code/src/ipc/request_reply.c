#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

/*	Sends a message to another device and waits for a reply on our own FIFO.
	Returns OK if we got the reply, or an error/timeout if something went wrong. */
int ipc_send_request_and_wait(const domo_message *request, domo_message *response, int fd_in)
{
    fd_set read_fds;
    struct timeval tv;
    int send_status;
    int retval;
	
	// Safety check: ensure the pointers exist and our listening pipe (fd_in) is valid.
    if (request == NULL || response == NULL || fd_in < 0) {
        return ERR_IPC_FAILURE;
    }
	
	// 1: Actually send the message out.
    send_status = ipc_send_message(request);
    if (send_status != OK) {
        return send_status;
    }

	/*	2: Prepare the "watch list" for the select() function.
		FD_ZERO clears the list, and FD_SET adds our specific file descriptor to it.*/
    FD_ZERO(&read_fds);
    FD_SET(fd_in, &read_fds);
	
	// 3: Set up the timer. We don't want to wait forever if the other device crashed
    tv.tv_sec = TIMEOUT_DEVICE;
    tv.tv_usec = 0;
	
	// 4: Wait for incoming data.
	/*	select() will pause our program here until either data arrives on fd_in, 
    	or the timer runs out. */
    retval = select(fd_in + 1, &read_fds, NULL, NULL, &tv);
    
    // If select returns a negative number, a system-level error occurred.
    if (retval < 0) {
        return ERR_IPC_FAILURE;
    }
	
	// If select returns 0, it means the timer ran out and no data arrived.
    if (retval == 0) {
        return ERR_TIMEOUT;
    }
	
	// 5: If we reach this point, select() returned a positive number.
    return ipc_recv_message(fd_in, response);
}
