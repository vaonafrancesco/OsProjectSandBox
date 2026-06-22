#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#include "../../include/protocol.h"
#include "../../include/ipc.h"
#include "../../include/error_codes.h"

// Opens a FIFO (named pipe) for a device so it can listen for incoming messages.
/*	It returns the file descriptor for reading, and passes back a "keepalive" 
	file descriptor through the pointer argument. */
int ipc_open_fifo_read(int my_id, int *keepalive_fd)
{
    char fifo_path[256];
    int fd_in;
    int fd_keep = -1;

	// 1. Build the exact file path where this device's FIFO should live
    snprintf(fifo_path, sizeof(fifo_path), "%s%d", FIFO_PATH_PREFIX, my_id);

	// 2. Create the physical file on the disk.
	/*	If mkfifo returns -1, it failed. BUT if errno is EEXIST, it just means 
    	the file was already created by a previous run  */
    if (mkfifo(fifo_path, 0666) == -1 && errno != EEXIST) {
        return ERR_IPC_FAILURE;
    }

	// 3. Open the pipe strictly for READING.
	/*	O_NONBLOCK: if there is no data, don't freeze the program, 
    	just return an error so we can do other things. */
    fd_in = open(fifo_path, O_RDONLY | O_NONBLOCK);
    if (fd_in < 0) {
        return ERR_IPC_FAILURE;
    }

	// 4. keepalive trick
	/*	In Linux, if a pipe is open for reading but NO ONE is currently holding it open 
    	for writing, the read() function will constantly return 0 (End Of File). 
    	This cause select() to spin in an infinite loop, to prevent this,
		the device opens its own pipe for writing but never actually writes to it */
    fd_keep = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_keep < 0) {
        close(fd_in);
        if (keepalive_fd != NULL) {
            *keepalive_fd = -1;
        }
        return ERR_IPC_FAILURE;
    }
    
    /*	5. Save the keepalive trick file descriptor so the caller
		can close it properly when the program shuts down. */
    
    if (keepalive_fd != NULL) {
        *keepalive_fd = fd_keep;
    } else {
    	// If the caller didn't provide a pointer, we close it
        close(fd_keep);
    }
	
	// Return the read-only file descriptor so the device can start listening
    return fd_in;
}
