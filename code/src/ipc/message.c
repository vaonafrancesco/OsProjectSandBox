#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../include/error_codes.h"
#include "../../include/ipc.h"
#include "../../include/protocol.h"
#include "../../include/serialization.h"

// Reads an incoming message from the pipe and converts it back into a struct.
int ipc_recv_message(int fd_in, domo_message *msg)
{
    char buffer[MAX_MSG_LEN];
    size_t total_read = 0;

	// Basic safety checks
    if (fd_in < 0 || msg == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

	// Clean up memory before we start
    memset(msg, 0, sizeof(*msg));
    memset(buffer, 0, sizeof(buffer));

    // Keep reading until the buffer is full
    while (total_read < sizeof(buffer) - 1) {
        char ch;
        ssize_t bytes_read;

		/*	Try to read exactly ONE character from the pipe.
        	If we get interrupted by a signal (EINTR), just try reading again. */
        do {
            bytes_read = read(fd_in, &ch, 1);
        } while (bytes_read < 0 && errno == EINTR);

		// Handle read errors
        if (bytes_read < 0) {
        	// EAGAIN or EWOULDBLOCK means the pipe is empty right now
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
            	/* 	If we haven't read anything yet, it's just a timeout. 
                	If we read half a message and then it stopped, the message is broken */
                return (total_read == 0) ? ERR_TIMEOUT : ERR_IPC_FAILURE;
            }
            return ERR_IPC_FAILURE;	
        }
		
		// If read returns 0, the other end of the pipe closed it (EOF)
        if (bytes_read == 0) {
            if (total_read == 0) {
                return ERR_TIMEOUT;	
            }
            return ERR_IPC_FAILURE;
        }
		
		// Save the character we just read into our buffer
        buffer[total_read++] = ch;
		
		// If we hit a newline, we know the message is complete!
        if (ch == NEWLINE_CHAR) {
            buffer[total_read] = '\0';
            // Send the string to be unpacked into the struct
            return deserialize_message(buffer, msg);
        }
    }

    return ERR_IPC_FAILURE;
}

// Converts a message struct into a string and writes it to the target device's pipe.
int ipc_send_message(const domo_message *msg)
{
    char fifo_path[256];
    char buffer[MAX_MSG_LEN];
    int fd_out;
    int rc;
    ssize_t total_written;
    ssize_t len;

    if (msg == NULL) {
        return ERR_IPC_FAILURE;
    }
	
	// 1. Figure out where we need to send this message based on the target_id
    if (snprintf(fifo_path, sizeof(fifo_path), "%s%d",
                 FIFO_PATH_PREFIX, msg->target_id) >= (int)sizeof(fifo_path)) {
        return ERR_IPC_FAILURE;
    }
    
	// 2. Pack the struct into a single string
    rc = serialize_message(msg, buffer, sizeof(buffer));
    if (rc != OK) {
        return rc;
    }
	
	/*	3. Open the target device's pipe. 
		O_NONBLOCK means we won't get stuck forever if the other device is broken. */
    fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_out < 0) {
        if (errno == ENXIO || errno == ENOENT) {
            return ERR_DEVICE_NOT_FOUND;	// Pipe doesn't exist, device is probably dead
        }
        return ERR_IPC_FAILURE;
    }

    len = (ssize_t)strlen(buffer);
    total_written = 0;
	
	// 4. Write the string to the pipe.
    while (total_written < len) {
        ssize_t n;
		
		/*	Try to write the remaining part of the string.
        	Ignore signal interrupts (EINTR) and just try again. */
        do {
            n = write(fd_out, buffer + total_written,
                      (size_t)(len - total_written));
        } while (n < 0 && errno == EINTR);

        if (n < 0) {
            close(fd_out);
            return ERR_IPC_FAILURE;
        }

        if (n == 0) {
            close(fd_out);
            return ERR_IPC_FAILURE;
        }

        total_written += n; // Keep track of how much we've successfully pushed in
    }

	// Clean up and close the pipe when we are done
    close(fd_out);
    return OK;
}
