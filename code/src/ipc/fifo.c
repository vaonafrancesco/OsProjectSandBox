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


//ipc_open_fifo_read
//Creates and opens the FIFO for reading safely.
// Uses a keep-alive writer to prevent read() from returning 0
int ipc_open_fifo_read(int my_id, int *keepalive_fd){
    char fifo_path[256];
    snprintf(fifo_path, sizeof(fifo_path), "%s%d", FIFO_PATH_PREFIX, my_id);

    // create the FIFO. If it exists already (EEXIST), proceed normally
	if(mkfifo(fifo_path, 0666)==-1 && errno != EEXIST){
		perror("Error creating FIFO");
		return ERR_IPC_FAILURE;
	}
	
	// Open the FIFO for reading in non-blocking mode to prevent the process from hanging indefinitely if no writer is currently attached.
	
	int fd_in = open(fifo_path, O_RDONLY | O_NONBLOCK);
	if(fd_in<0){
		perror("Error opening FIFO for read");
		return ERR_IPC_FAILURE;
	}
	
	// Open a persistent file descriptor in write mode to keep the pipe open.
	// this will make the read() call wont block and wait for new messages
	
	if (keepalive_fd != NULL){
		*keepalive_fd = open(fifo_path, O_WRONLY);
	}
	
	// REmove the O_NONBLOCK flag so that read() works normally (blocking wait)
	int flags = fcntl (fd_in, F_GETFL);
	fcntl(fd_in, F_SETFL, flags & ~O_NONBLOCK);
	
	return fd_in;
		

}
