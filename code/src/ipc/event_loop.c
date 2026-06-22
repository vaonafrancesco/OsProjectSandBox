#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>

#include "common.h"
#include "controller.h"
#include "error_codes.h"
#include "ipc.h"
#include "repl.h"
#include "cleanup.h"

// Wrapper to pass the keyboard input to the REPL (Read-Eval-Print Loop)
static int handle_stdin_event(controller *ctrl)
{
    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    return repl_read_and_execute(ctrl);
}

/*	Used for printing asynchronous messages from devices without breaking 
	the user's terminal prompt. Currently commented out to keep the terminal clean.*/
static void eventloop_print_async_line(const char *fmt, ...)
{
    // va_list ap;

    if (fmt == NULL) {
        return;
    }

    // Debug log - commentato per ora 
    // fprintf(stderr, "\r");
    // va_start(ap, fmt);
    // vfprintf(stderr, fmt, ap);
    // va_end(ap);
    // fprintf(stderr, "\n");
    // fflush(stderr);
    
    (void)fmt;  // Parameter unused after debug logs commented out
}

// Cleans up a request that we were waiting for 
static void eventloop_discard_pending(pending_request *req)
{
    if (req == NULL) {
        return;
    }

    if (req->reply_fd >= 0) {
        close(req->reply_fd);
    }

    if (req->reply_fifo_path[0] != '\0') {
        unlink(req->reply_fifo_path);
    }

    memset(req, 0, sizeof(*req));
    req->reply_fd = -1;
}

// Reads messages that arrive completely out-of-the-blue on the controller's main FIFO.
static int handle_controller_fifo_event(controller *ctrl, int fifo_fd)
{
    int handled_any = 0;

    if (ctrl == NULL || fifo_fd < 0) {
        return ERR_INVALID_PARAMETERS;
    }
	
	// Keep reading until the pipe is empty
    for (;;) {
        domo_message msg;
        int rc;

        memset(&msg, 0, sizeof(msg));
        rc = ipc_recv_message(fifo_fd, &msg);
		
		// If the pipe is empty, we are done here
        if (rc == ERR_TIMEOUT) {
            return handled_any ? OK : ERR_TIMEOUT;
        }

        if (rc != OK) {
            return rc;
        }

        handled_any = 1;
		
		// Print status updates sent by devices
        if (strcmp(msg.command, "STATUS") == 0) {
            eventloop_print_async_line(
                "[status] sender=%s target=%d payload=%s",
                (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
                msg.target_id,
                (msg.payload[0] != '\0') ? msg.payload : "(empty)"
            );
            continue;
        }
		
		// Print override alerts 
        if (strcmp(msg.command, "OVERRIDE") == 0) {
            eventloop_print_async_line(
                "[override] sender=%s target=%d payload=%s",
                (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
                msg.target_id,
                (msg.payload[0] != '\0') ? msg.payload : "(empty)"
            );
            continue;
        }
		
		// Catch-all for other unexpected IPC messages
        eventloop_print_async_line(
            "[%s] sender=%s target=%d payload=%s",
            (msg.command[0] != '\0') ? msg.command : "ipc",
            (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
            msg.target_id,
            (msg.payload[0] != '\0') ? msg.payload : "(empty)"
        );
    }
}

// Checks all the temporary reply FIFOs we created to see if any device has answered our requests.
static int handle_pending_reply_fds(controller *ctrl, fd_set *readfds)
{
    int i;

    if (ctrl == NULL || readfds == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    for (i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
        pending_request *req = &ctrl->pending[i];
        int rc;

        if (!req->in_use || req->reply_fd < 0) {
            continue;
        }

        if (!FD_ISSET(req->reply_fd, readfds)) {
            continue;
        }

        rc = controller_complete_pending_fd(ctrl, req->reply_fd);
        if (rc == ERR_TIMEOUT) {
            continue;
        }
		
		// If something went wrong, print a helpful error message depending on the request type
        if (rc != OK) {
            if (req->kind == CTRL_REQ_INFO && device_is_control(req->target_type)) {
                eventloop_print_async_line(
                    "%s id=%d state=manual_override error=consistency_check_failed",
                    device_type_str(req->target_type),
                    req->target_id
                );
            } else if (req->kind == CTRL_REQ_INFO) {
                eventloop_print_async_line(
                    "info %d failed: %s",
                    req->target_id,
                    error_str(rc)
                );
            } else if (req->kind == CTRL_REQ_SWITCH) {
                eventloop_print_async_line(
                    "switch %d failed: %s",
                    req->target_id,
                    error_str(rc)
                );
            }
			
			// Free the slot
            eventloop_discard_pending(req);
        }
    }

    return OK;
}

// The main event loop
// This keeps the controller alive and responding to everything around it.
int event_loop_run(controller *ctrl)
{
    int fifo_fd;
    int keepalive_fd;
    int rc;
    int prompt_visible;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
	
	// Open the controller's main listening pipe
    fifo_fd = ipc_open_fifo_read(CONTROLLER_ID, &keepalive_fd);
    if (fifo_fd < 0) {
        return ERR_IPC_FAILURE;
    }

    prompt_visible = 0;
	
	// Keep spinning until the user types 'exit'
    while (ctrl->running) {
        fd_set readfds;
        int max_fd;
        struct timeval tv;
        int i;
		
		// Clean up any dead child processes so they don't become "zombies"
        if (cleanup_has_pending_sigchld()) {
            cleanup_reap_terminated_children(ctrl);
            prompt_visible = 0;
        }
		
		// Check if any pending requests took too long and need to be cancelled
        controller_expire_pending(ctrl);
		
		// SETUP SELECT()
		// We tell select() which file descriptors we want to monitor.
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(fifo_fd, &readfds);
		
		// select() needs to know the highest FD number it should check
        max_fd = (STDIN_FILENO > fifo_fd) ? STDIN_FILENO : fifo_fd;
		
		// Also add all the temporary reply pipes to the watch list
        for (i = 0; i < CONTROLLER_MAX_PENDING; ++i) {
            pending_request *req = &ctrl->pending[i];

            if (!req->in_use || req->reply_fd < 0) {
                continue;
            }

            FD_SET(req->reply_fd, &readfds);
            if (req->reply_fd > max_fd) {
                max_fd = req->reply_fd;
            }
        }
		
		// Print "domotics>" if we need to
        if (!prompt_visible) {
            repl_print_prompt();
            prompt_visible = 1;
        }
		
		// Wake up every 200 milliseconds just to run background tasks.
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
		
		// Pause here until someone types, a pipe gets data, or 200ms pass.
        rc = select(max_fd + 1, &readfds, NULL, NULL, &tv);
        if (rc < 0) {
        	// If a signal interrupted the wait, handle it and restart the loop
            if (errno == EINTR) {
                if (cleanup_has_pending_sigchld()) {
                    cleanup_reap_terminated_children(ctrl);
                    prompt_visible = 0;
                }
                continue;
            }

            close(fifo_fd);
            close(keepalive_fd);
            return ERR_SYSTEM;
        }

        if (cleanup_has_pending_sigchld()) {
            cleanup_reap_terminated_children(ctrl);
            prompt_visible = 0;
        }

        controller_expire_pending(ctrl);

        if (rc == 0) {
            continue;
        }
		
		// EVENT: KEYBOARD INPUT
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            rc = handle_stdin_event(ctrl);
            prompt_visible = 0;
		 	// If the user typed "exit", the controller stops running.
            if (!ctrl->running) {
               int retries = 5; 
               
               // MINI-FLUSH LOOP:
                /*	Give the devices a tiny bit of time (5 tries of 100ms) to send 
               		any final replies before we completely destroy the controller. */
                while (retries-- > 0) {
                    fd_set flush_fds;
                    int flush_max_fd = -1;
                    int pending_count = 0;
                    struct timeval tv;
                    int j;
					
                    FD_ZERO(&flush_fds);
                    for (j = 0; j < CONTROLLER_MAX_PENDING; ++j) {
                        if (ctrl->pending[j].in_use && ctrl->pending[j].reply_fd >= 0) {
                            FD_SET(ctrl->pending[j].reply_fd, &flush_fds);
                            if (ctrl->pending[j].reply_fd > flush_max_fd) {
                                flush_max_fd = ctrl->pending[j].reply_fd;
                            }
                            pending_count++;
                        }
                    }
                
                    if (pending_count == 0) {
                        break; 
                    }
                   
                    tv.tv_sec = 0;
                    tv.tv_usec = 100000; 
                                 
                    if (select(flush_max_fd + 1, &flush_fds, NULL, NULL, &tv) > 0) {
                        for (j = 0; j < CONTROLLER_MAX_PENDING; ++j) {
                            if (ctrl->pending[j].in_use && ctrl->pending[j].reply_fd >= 0) {
                                if (FD_ISSET(ctrl->pending[j].reply_fd, &flush_fds)) {
                                    controller_complete_pending_fd(ctrl, ctrl->pending[j].reply_fd);
                                }
                            }
                        }
                    }
                }
                break;
            }

            if (rc != OK) {
                fprintf(stderr, "Input error: %s\n", error_str(rc));
            }
        }
		
		// EVENT: INCOMING BACKGROUND MESSAGE
        if (ctrl->running && FD_ISSET(fifo_fd, &readfds)) {
            rc = handle_controller_fifo_event(ctrl, fifo_fd);
            prompt_visible = 0;

            if (rc != OK && rc != ERR_TIMEOUT) {
                fprintf(stderr, "IPC error: %s\n", error_str(rc));
                prompt_visible = 0;
            }
        }
		
		// EVENT: REPLY RECEIVED
        if (ctrl->running) {
            rc = handle_pending_reply_fds(ctrl, &readfds);
            prompt_visible = 0;

            if (rc != OK) {
                fprintf(stderr, "Pending IPC error: %s\n", error_str(rc));
            }
        }

        controller_expire_pending(ctrl);
    }
	
	// Shut down resources before exiting
    close(fifo_fd);
    close(keepalive_fd);
    return OK;
}
