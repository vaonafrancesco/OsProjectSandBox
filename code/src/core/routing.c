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

/* Global routing table */
routing_node routing_table[MAX_DEVICES];

static int routing_find_index(int id)
{
    int i;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id == id) {
            return i;
        }
    }

    return -1;
}

/* Initializes the routing table with all empty slots. */
void routing_init(void)
{
    int i;

    for (i = 0; i < MAX_DEVICES; i++) {
        routing_table[i].id = -1;
        routing_table[i].parent_id = -1;
        routing_table[i].type = DEVICE_UNKNOWN;
    }
}

/* Register a new device in the system, defaulting parent to controller. */
int routing_add_node(int id, device_type type)
{
    int i;

    if (id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

	// Make sure the device isn't already in the table to avoid duplicates
    if (routing_find_index(id) >= 0) {
        return ERR_NOT_ALLOWED;
    }

	// Find the first empty slot and put the new device there
    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id == -1) {
            routing_table[i].id = id;
            routing_table[i].parent_id = CONTROLLER_ID;
            routing_table[i].type = type;
            return OK;
        }
    }

	// If we finish the loop without returning, the array is full
    return ERR_NOT_ALLOWED;
}

/* Remove a device from the routing table and detach its direct children. */
int routing_remove_node(int id)
{
    int i;
    int found = 0;
	
	//1: Find the device and erase it
    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id == id) {
            routing_table[i].id = -1;
            routing_table[i].parent_id = -1;
            routing_table[i].type = DEVICE_UNKNOWN;
            found = 1;
            break;
        }
    }

    if (!found) {
        return ERR_DEVICE_NOT_FOUND;
    }
	
	//2: Find all its children and make them orphans (parent_id = -1)
    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id >= 0 && routing_table[i].parent_id == id) {
            routing_table[i].parent_id = -1;
        }
    }

    return OK;
}

// Retrieves the parent ID of a specific device
int routing_get_parent_id(int id, int *parent_id_out)
{
    int i;

    if (parent_id_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id == id) {
            *parent_id_out = routing_table[i].parent_id;
            return OK;
        }
    }

    return ERR_DEVICE_NOT_FOUND;
}

// Fills an array with all the IDs of the children connected to a specific parent
int routing_collect_children(int parent_id, device_id *children_out, int max_children, int *count_out)
{
    int i;
    int n = 0;

    if (children_out == NULL || count_out == NULL || max_children <= 0) {
        return ERR_INVALID_PARAMETERS;
    }

    *count_out = 0;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id >= 0 && routing_table[i].parent_id == parent_id) {
            if (n >= max_children) {
                return ERR_NOT_ALLOWED;	// Buffer is not big enough
            }
            children_out[n++] = (device_id)routing_table[i].id;
        }
    }

    *count_out = n;
    return OK;
}

/* Build reply FIFO path from pid + request id. */
int make_reply_fifo_path(pid_t pid, int request_id, char *path, size_t path_len)
{
    int written;

    if (path == NULL || path_len == 0 || pid <= 0 || request_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    written = snprintf(path, path_len, "%s%d_%d.fifo",
                       FIFO_PATH_PREFIX, (int)pid, request_id);
    if (written < 0 || (size_t)written >= path_len) {
        return ERR_IPC_FAILURE;
    }

    return OK;
}

/* Build device FIFO path from device id. */
int make_device_fifo_path(device_id id, char *path, size_t path_len)
{
    int written;

    if (path == NULL || path_len == 0 || id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    written = snprintf(path, path_len, "%s%d", FIFO_PATH_PREFIX, id);
    if (written < 0 || (size_t)written >= path_len) {
        return ERR_IPC_FAILURE;
    }

    return OK;
}

/* Send a serialized domo_message to a specific FIFO path without blocking forever. */
int send_message_to_fifo(const char *fifo_path, const domo_message *msg)
{
    int fd_out;
    char buffer[MAX_MSG_LEN];
    int len;
    ssize_t written_total = 0;

    if (fifo_path == NULL || msg == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

	// Convert the struct into a pipe-separated string
    len = snprintf(buffer, sizeof(buffer), "%s|%s|%d|%d|%d|%d|%d|%s|%s|%d|%s\n",
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

    if (len < 0 || len >= (int)sizeof(buffer)) {
        return ERR_IPC_FAILURE;
    }

	// Open the pipe for writing.
    fd_out = open(fifo_path, O_WRONLY | O_NONBLOCK);
    if (fd_out < 0) {
        if (errno == ENXIO || errno == ENOENT) {
            return ERR_DEVICE_NOT_FOUND;
        }
        return ERR_IPC_FAILURE;
    }

	/*	Keep writing until the whole message is sent.
		The OS might not write the whole string in one go, so a while loop is needed. */
    while (written_total < len) {
        ssize_t n = write(fd_out, buffer + written_total, (size_t)(len - written_total));

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            close(fd_out);
            return ERR_IPC_FAILURE;
        }

        if (n == 0) {
            close(fd_out);
            return ERR_IPC_FAILURE;	// Pipe was closed unexpectedly
        }

        written_total += n;
    }

    close(fd_out);
    return OK;
}

/* Send a request and wait up to timeout_sec for reply on a temporary FIFO. */
int request_reply_timeout(const char *target_fifo, const char *reply_fifo,
                          const domo_message *request, domo_message *response,
                          int timeout_sec)
{
    int fd_reply;
    int rc;
    fd_set read_fds;
    struct timeval tv;
    int retval;

    if (target_fifo == NULL || reply_fifo == NULL ||
        request == NULL || response == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (timeout_sec <= 0) {
        return ERR_INVALID_PARAMETERS;
    }
	
	// 1: Create our temporary "return envelope" FIFO
    unlink(reply_fifo);
    if (mkfifo(reply_fifo, 0666) != 0 && errno != EEXIST) {
        return ERR_SYSTEM;
    }

    fd_reply = open(reply_fifo, O_RDONLY | O_NONBLOCK);
    if (fd_reply < 0) {
        unlink(reply_fifo);
        return ERR_SYSTEM;
    }

	// 2: Send the actual request to the target
    rc = send_message_to_fifo(target_fifo, request);
    if (rc != OK) {
        close(fd_reply);
        unlink(reply_fifo);
        return rc;
    }

	// 3: Wait for the answer using select()
    FD_ZERO(&read_fds);
    FD_SET(fd_reply, &read_fds);

    tv.tv_sec = timeout_sec;
    tv.tv_usec = 0;
	
	// Wait until data arrives OR the timeout runs out.
    // If we get an EINTR (signal interruption), we restart the select loop.
    do {
        retval = select(fd_reply + 1, &read_fds, NULL, NULL, &tv);
    } while (retval < 0 && errno == EINTR);

    if (retval < 0) {	// Actual system error
        close(fd_reply);
        unlink(reply_fifo);
        return ERR_IPC_FAILURE;
    }

    if (retval == 0) {	// The timer ran out before we got an answer!
        close(fd_reply);
        unlink(reply_fifo);
        return ERR_TIMEOUT;
    }
	
	// 4: We got data! Read it into the response struct
    rc = ipc_recv_message(fd_reply, response);
	
	// 5: Clean up the temporary FIFO so we don't leave trash on the disk
    close(fd_reply);
    unlink(reply_fifo);
    return rc;
}

int request_reply(const char *target_fifo, const char *reply_fifo,
                  const domo_message *request, domo_message *response)
{
    return request_reply_timeout(target_fifo, reply_fifo, request, response, TIMEOUT_DEVICE);
}
