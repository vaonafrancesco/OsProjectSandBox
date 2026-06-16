#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdlib.h>

#include "common.h"
#include "controller.h"
#include "error_codes.h"
#include "ipc.h"
#include "repl.h"
#include "cleanup.h"
#include <stdarg.h>

static int handle_stdin_event(controller *ctrl) {
    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    return repl_read_and_execute(ctrl);
}

static void eventloop_print_async_line(const char *fmt, ...)
{
    va_list ap;

    if (fmt == NULL) {
        return;
    }

    fprintf(stderr, "\r");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static int handle_controller_fifo_event(controller *ctrl, int fifo_fd)
{
    int handled_any = 0;

    if (ctrl == NULL || fifo_fd < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    for (;;) {
        domo_message msg;
        int rc;

        memset(&msg, 0, sizeof(msg));
        rc = ipc_recv_message(fifo_fd, &msg);

        if (rc != OK) {
            return handled_any ? OK : rc;
        }

        handled_any = 1;

        if (strcmp(msg.command, "STATUS") == 0) {
            eventloop_print_async_line(
                "[status] sender=%s target=%d payload=%s",
                (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
                msg.target_id,
                (msg.payload[0] != '\0') ? msg.payload : "(empty)"
            );
            continue;
        }

        if (strcmp(msg.command, "OVERRIDE") == 0) {
            eventloop_print_async_line(
                "[override] sender=%s target=%d payload=%s",
                (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
                msg.target_id,
                (msg.payload[0] != '\0') ? msg.payload : "(empty)"
            );
            continue;
        }

        eventloop_print_async_line(
            "[%s] sender=%s target=%d payload=%s",
            (msg.command[0] != '\0') ? msg.command : "ipc",
            (msg.sender_id[0] != '\0') ? msg.sender_id : "?",
            msg.target_id,
            (msg.payload[0] != '\0') ? msg.payload : "(empty)"
        );
    }
}

int event_loop_run(controller *ctrl) {
    int fifo_fd;
    int keepalive_fd;
    int max_fd;
    int rc;
    int prompt_visible;

    if (ctrl == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    fifo_fd = ipc_open_fifo_read(CONTROLLER_ID, &keepalive_fd);
    if (fifo_fd < 0) {
        return ERR_IPC_FAILURE;
    }

    prompt_visible = 0;

    while (ctrl->running) {
        fd_set readfds;

        if (cleanup_has_pending_sigchld()) {
            cleanup_reap_terminated_children(ctrl);
            prompt_visible = 0;
        }

        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(fifo_fd, &readfds);

        max_fd = (STDIN_FILENO > fifo_fd) ? STDIN_FILENO : fifo_fd;

        if (!prompt_visible) {
            repl_print_prompt();
            prompt_visible = 1;
        }

        rc = select(max_fd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
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

        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            rc = handle_stdin_event(ctrl);
            prompt_visible = 0;

            if (!ctrl->running) {
                break;
            }

            if (rc != OK) {
                fprintf(stderr, "Input error: %s\n", error_str(rc));
            }
        }

        if (ctrl->running && FD_ISSET(fifo_fd, &readfds)) {
            rc = handle_controller_fifo_event(ctrl, fifo_fd);
            prompt_visible = 0;

            if (rc != OK) {
                fprintf(stderr, "IPC error: %s\n", error_str(rc));
                prompt_visible = 0;
            }
        }
    }

    close(fifo_fd);
    close(keepalive_fd);
    return OK;
}