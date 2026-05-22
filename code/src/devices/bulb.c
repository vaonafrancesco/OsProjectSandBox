#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common.h"
#include "error_codes.h"
#include "ipc.h"
#include "routing.h"
#include "protocol.h"

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    device_id id;
    state state;
    unsigned long usage_time;
    char fifo_path[PATH_MAX];
} bulb_ctx ;

static void on_sigterm(int sig) {
    (void)sig;
    keep_running = 0;
}

static void simulate_delay(void) {
    int delay = MIN_RANDOM_DELAY_S;
    if (MAX_RANDOM_DELAY_S > MIN_RANDOM_DELAY_S) {
        delay += rand() % (MAX_RANDOM_DELAY_S - MIN_RANDOM_DELAY_S + 1);
    }
    sleep((unsigned int)delay);
}

static const char *state_str(state state) {
    switch (state) {
        case STATE_ON: return "on";
        case STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int build_info_payload(const bulb_ctx *ctx, char *buf, size_t len) {
    if (ctx == NULL || buf == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    snprintf(buf, len,
             "bulb id=%d state=%s time=%lu fifo=%s",
             ctx->id, state_str(ctx->state), ctx->usage_time, ctx->fifo_path);
    return OK;
}

static int handle_request(bulb_ctx *ctx, const domo_message *req, domo_message *resp) {
    if (ctx == NULL || req == NULL || resp == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(resp, 0, sizeof(*resp));
    resp->kind = MSG_RESPONSE;
    snprintf(resp->command, sizeof(resp->command), "%s", req->command);
    resp->src_id = ctx->id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = OK;

    simulate_delay();

    if (strcmp(req->command, CMD_INFO) == 0) {
        return build_info_payload(ctx, resp->payload, sizeof(resp->payload));
    }

    if (strcmp(req->command, CMD_SWITCH) == 0) {
            if (strcmp(req->arg1, "power") != 0) {
                resp->status = ERR_INVALID_PARAMETERS;
                return OK;
            }

            if (strcmp(req->arg2, "on") == 0) {
                ctx->state = STATE_ON;
            } else if (strcmp(req->arg2, "off") == 0) {
                ctx->state = STATE_OFF;
            } else {
                resp->status = ERR_INVALID_PARAMETERS;
                return OK;
            }

            snprintf(resp->payload, sizeof(resp->payload),
                     "bulb %d switched %s", ctx->id, state_str(ctx->state));
            return OK;
        }
        
            resp->status = ERR_INVALID_COMMAND;
            return OK;
}

int bulb_device_main(device_id id) {
    bulb_ctx ctx;
    domo_message req;
    domo_message resp;
    int fd;
    int dummy_fd;

    memset(&ctx, 0, sizeof(ctx));
    ctx.id = id;
    ctx.state = STATE_OFF;

    if (make_device_fifo_path(id, ctx.fifo_path, sizeof(ctx.fifo_path)) != OK) {
        return ERR_SYSTEM;
    }

    unlink(ctx.fifo_path);
    if (mkfifo(ctx.fifo_path, 0666) != 0 && errno != EEXIST) {
        return ERR_SYSTEM;
    }

    signal(SIGTERM, on_sigterm);
    srand((unsigned int)(getpid() ^ id));

    fd = open(ctx.fifo_path, O_RDONLY);
    if (fd < 0) {
        unlink(ctx.fifo_path);
        return ERR_SYSTEM;
    }

    dummy_fd = open(ctx.fifo_path, O_WRONLY | O_NONBLOCK);

    while (keep_running) {
        int rc = ipc_recv_message(fd, &req);
        if (rc != OK) {
            continue;
        }

        rc = handle_request(&ctx, &req, &resp);
        if (rc != OK) {
            continue;
        }

        if (req.payload[0] != '\0') {
            rc = send_message_to_fifo(req.payload, &resp);
        } else {
            char reply_fifo[PATH_MAX];
            rc = make_reply_fifo_path(req.src_pid, req.request_id, reply_fifo, sizeof(reply_fifo));
            if (rc == OK) {
                rc = send_message_to_fifo(reply_fifo, &resp);
            }
        }

        (void)rc;
    }

    close(fd);
    if (dummy_fd >= 0) close(dummy_fd);
    unlink(ctx.fifo_path);
    return OK;
}