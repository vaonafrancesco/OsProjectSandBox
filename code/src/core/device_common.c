#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include "device.h"
#include "error_codes.h"
#include "ipc.h"
#include "protocol.h"
#include "routing.h"
#include "utils.h"

static volatile sig_atomic_t device_keep_running = 1;

// Simple helper to convert the device type enum into a readable string
const char *device_type_str(device_type type) {
    switch (type) {
        case DEVICE_CONTROLLER: return "controller";
        case DEVICE_HUB: return "hub";
        case DEVICE_TIMER: return "timer";
        case DEVICE_BULB: return "bulb";
        case DEVICE_WINDOW: return "window";
        case DEVICE_FRIDGE: return "fridge";
        default: return "unknown";
    }
}

// Checks if the device is a "parent/manager" type
bool device_is_control(device_type type) {
    return type == DEVICE_CONTROLLER || type == DEVICE_HUB || type == DEVICE_TIMER;
}

// Checks if the device is a "leaf/actuator" type
bool device_is_interaction(device_type type) {
    return type == DEVICE_BULB || type == DEVICE_WINDOW || type == DEVICE_FRIDGE;
}

/*	This function is triggered by the OS when the process receives a SIGTERM (kill signal).
	It safely asks the main loop to stop instead of crashing the program instantly. */
static void device_on_sigterm(int sig) {
    (void)sig;
    device_keep_running = 0;
}

// Helper to check if the incoming message is telling us to delete ourselves
static bool device_is_del_command(const domo_message *req) {
    return req != NULL && strcmp(req->command, CMD_DEL) == 0;
}

/*	Checks if the sender expects an answer back.
	We don't reply to "child removed" notifications to prevent infinite loops. */
static int device_message_requires_reply(const domo_message *req)
{
    if (req == NULL) {
        return 0;
    }

    if (strcmp(req->command, CMD_CHILD_REMOVED) == 0) {
        return 0;
    }

    return 1;
}

// Helper to check if a message is a "child removed" notification
static bool device_is_child_removed_command(const domo_message *req)
{
    return req != NULL && strcmp(req->command, CMD_CHILD_REMOVED) == 0;
}

/*	If a parent is notified that one of its children died, it removes the child
	from its internal array by replacing it with the last element and shrinking the array.*/
static void device_handle_child_removed(device *dev, const domo_message *req)
{
    device_id removed_id;

    if (dev == NULL || req == NULL || dev->child_ids == NULL || dev->child_count == 0) {
        return;
    }

    if (!device_is_child_removed_command(req)) {
        return;
    }

    removed_id = (device_id)atoi(req->payload);

    for (size_t i = 0; i < dev->child_count; ++i) {
        if (dev->child_ids[i] == removed_id) {
            dev->child_ids[i] = dev->child_ids[dev->child_count - 1];	// Swap with the last one
            dev->child_count--;
            return;
        }
    }
}
	
/*	Populates the base device struct with default values.
	This is called before the specific device adds its own custom data.*/
int device_common_init(device *dev, device_id id, device_type type) {
    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(dev, 0, sizeof(*dev));
    dev->info.id = id;
    dev->info.type = type;
    dev->info.pid = getpid();
    dev->info.logical_parent_id = NO_PARENT;
    dev->info.state = STATE_OFF;
    dev->info.manual_override = false;
	
	// Generate the path for this specific device's FIFO
    if (make_device_fifo_path(id, dev->info.fifo_path, sizeof(dev->info.fifo_path)) != OK) {
        return ERR_SYSTEM;
    }

    snprintf(dev->info.name, sizeof(dev->info.name), "%s_%d", device_type_str(type), id);
    return OK;
}

// Creates the physical FIFO file on the disk and hooks up the signal handler
int device_common_setup_fifo(device *dev)
{
    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    unlink(dev->info.fifo_path);	// Clear any leftover garbage from previous runs
    if (mkfifo(dev->info.fifo_path, 0666) != 0 && errno != EEXIST) {
        return ERR_SYSTEM;
    }

    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = device_on_sigterm;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGTERM, &sa, NULL);
    }
	
	// Seed the random number generator using PID and ID for variety
    srand((unsigned int)(getpid() ^ dev->info.id));
    return OK;
}

// Opens the FIFO for reading so the device can receive commands
int device_common_open_fifo(device *dev, int *fd_out, int *dummy_fd_out) {
    int fd, dummy_fd;

    if (dev == NULL || fd_out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    fd = open(dev->info.fifo_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("open O_RDONLY | O_NONBLOCK failed");
        unlink(dev->info.fifo_path);
        return ERR_SYSTEM;
    }

    dummy_fd = open(dev->info.fifo_path, O_WRONLY | O_NONBLOCK);

    *fd_out = fd;
    if (dummy_fd_out != NULL) {
        *dummy_fd_out = dummy_fd;
    }

    return OK;
}

// This infinite loop listens to the FIFO, processes commands, and answers back.
int device_common_main_loop(device *dev, int fd)
{
    int rc;

    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    while (device_keep_running) {
        fd_set readfds;
        struct timeval tv;

        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
		
		// Sleep for 1 second max, then wake up to check things (timeouts or updates)
        tv.tv_sec = 1;
        tv.tv_usec = 0;
		
		// select() blocks until there is data to read on 'fd', or the 1 second timer runs out
        rc = select(fd + 1, &readfds, NULL, NULL, &tv);

        if (rc < 0) {
            if (errno == EINTR) {	// Interrupted by a signal, just try again
                continue;
            }
            return ERR_SYSTEM;
        }

        if (!device_keep_running) {
            break;	// Someone sent a SIGTERM
        }
		
		
		// Let the specific device update its internal state
        if (dev->update != NULL) {
            dev->update(dev);
        }
		
		// If rc > 0 and FD_ISSET is true, someone sent us a message!
        if (rc > 0 && FD_ISSET(fd, &readfds)) {
            for (;;) {
                domo_message req;
                domo_message resp;
                int needs_reply;

                memset(&req, 0, sizeof(req));
                memset(&resp, 0, sizeof(resp));
				
				// Read from the FIFO
                rc = ipc_recv_message(fd, &req);
                if (rc != OK) {
                    if (rc == ERR_TIMEOUT || rc == ERR_IPC_FAILURE ||
                        errno == EAGAIN || errno == EWOULDBLOCK) {
                        break;
                    }
                    if (!device_keep_running) {
                        break;
                    }
                    break;
                }

                // Debug log - commented out
                // fprintf(stderr,
                //         "[device %d] recv cmd=%s src=%d dst=%d arg1=%s arg2=%s payload=%s req=%d\n",
                //         dev->info.id,
                //         req.command,
                //         req.src_id,
                //         req.dst_id,
                //         req.arg1,
                //         req.arg2,
                //         req.payload,
                //         req.request_id);
                // fflush(stderr);
				
				// If it's a kill command, simulate a delay, break the loop, and die
                if (device_is_del_command(&req)) {
                    simulate_random_delay();
                    device_keep_running = 0;
                    break;
                }
				
				// Small parser to handle arguments for the "switch" command
                if (strcmp(req.command, CMD_SWITCH) == 0 &&
                    req.arg1[0] == '\0' &&
                    req.payload[0] != '\0') {
                    char label[sizeof(req.arg1)] = {0};
                    char pos[sizeof(req.arg2)] = {0};

                    if (sscanf(req.payload, "%31[^ ,],%31s", label, pos) == 2 ||
                        sscanf(req.payload, "%31s %31s", label, pos) == 2) {
                        snprintf(req.arg1, sizeof(req.arg1), "%s", label);
                        snprintf(req.arg2, sizeof(req.arg2), "%s", pos);
                    }
                }
				
				// If a child died, update our internal lists
                device_handle_child_removed(dev, &req);
				
				// Pass the message to the specific device's custom logic 
                if (dev->handle_message == NULL) {
                    continue;
                }

                rc = dev->handle_message(dev, &req, &resp);
                if (rc != OK) {
                    continue;
                }
				
				// Does the sender want a response?
                needs_reply = device_message_requires_reply(&req);
                if (!needs_reply) {
                    continue;
                }
				
				// Open the sender's dynamic reply FIFO and send the response back
                {
                    char reply_fifo[PATH_MAX];

                    rc = make_reply_fifo_path(req.src_pid,
                                              req.request_id,
                                              reply_fifo,
                                              sizeof(reply_fifo));
                    if (rc == OK) {
                        rc = send_message_to_fifo(reply_fifo, &resp);
                        if (rc != OK) {
                            // Debug log - commented out
                            /**
                             * Ogni volta che un dispositivo riceve un messaggio IPC
Cosa stampa: Tutti i dettagli del messaggio ricevuto (comando, sorgente, destinazione, argomenti, payload)
                             */
                            // fprintf(stderr,
                            //         "[device %d] failed reply cmd=%s req=%d rc=%d\n",
                            //         dev->info.id,
                            //         req.command,
                            //         req.request_id,
                            //         rc);
                            // fflush(stderr);
                        }
                    }
                }
            }
        }
    }

    return OK;
}

// Clean up everything before the process exits
int device_common_cleanup(device *dev, int fd, int dummy_fd)
{
    if (dev == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    if (fd >= 0) {
        close(fd);
    }
    if (dummy_fd >= 0) {
        close(dummy_fd);
    }

    unlink(dev->info.fifo_path);	// Delete the physical file
		
	// Call the custom destruction logic if the specific device has one
    if (dev->destroy != NULL) {
        dev->destroy(dev);
    }

    return OK;
}

// Utility to build a neat string containing all the device's state info
int device_build_info_payload(const device *dev, char *buffer, size_t buffer_len) {
    if (dev == NULL || buffer == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    snprintf(buffer, buffer_len,
             "device id=%d type=%s state=%d parent=%d pid=%d fifo=%s",
             dev->info.id,
             device_type_str(dev->info.type),
             dev->info.state,
             dev->info.logical_parent_id,
             (int)dev->info.pid,
             dev->info.fifo_path);
    return OK;
}
