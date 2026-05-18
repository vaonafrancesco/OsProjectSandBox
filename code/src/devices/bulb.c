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

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    device_id_t id;
    domo_state_t state;
    unsigned long usage_time;
    char fifo_path[DOMO_PATH_MAX];
} bulb_ctx_t;

static void on_sigterm(int sig) {
    (void)sig;
    keep_running = 0;
}

static void simulate_delay(void) {
    int delay = DOMO_MIN_RANDOM_DELAY_S;
    if (DOMO_MAX_RANDOM_DELAY_S > DOMO_MIN_RANDOM_DELAY_S) {
        delay += rand() % (DOMO_MAX_RANDOM_DELAY_S - DOMO_MIN_RANDOM_DELAY_S + 1);
    }
    sleep((unsigned int)delay);
}

static const char *state_str(domo_state_t state) {
    switch (state) {
        case DOMO_STATE_ON: return "on";
        case DOMO_STATE_OFF: return "off";
        default: return "unknown";
    }
}

static int build_info_payload(const bulb_ctx_t *ctx, char *buf, size_t len) {
    if (ctx == NULL || buf == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    snprintf(buf, len,
             "bulb id=%d state=%s time=%lu fifo=%s",
             ctx->id, state_str(ctx->state), ctx->usage_time, ctx->fifo_path);
    return DOMO_OK;
}

static int handle_request(bulb_ctx_t *ctx, const domo_message_t *req, domo_message_t *resp) {
    if (ctx == NULL || req == NULL || resp == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    memset(resp, 0, sizeof(*resp));
    resp->kind = DOMO_MSG_RESPONSE;
    resp->cmd = req->cmd;
    resp->src_id = ctx->id;
    resp->dst_id = req->src_id;
    resp->src_pid = getpid();
    resp->request_id = req->request_id;
    resp->status = DOMO_OK;

    simulate_delay();

    switch (req->cmd) {
        case DOMO_CMD_INFO:
            return build_info_payload(ctx, resp->payload, sizeof(resp->payload));

        case DOMO_CMD_SWITCH:
            if (strcmp(req->arg1, "power") != 0) {
                resp->status = DOMO_ERR_INVALID_PARAMETERS;
                return DOMO_OK;
            }

            if (strcmp(req->arg2, "on") == 0) {
                ctx->state = DOMO_STATE_ON;
            } else if (strcmp(req->arg2, "off") == 0) {
                ctx->state = DOMO_STATE_OFF;
            } else {
                resp->status = DOMO_ERR_INVALID_PARAMETERS;
                return DOMO_OK;
            }

            snprintf(resp->payload, sizeof(resp->payload),
                     "bulb %d switched %s", ctx->id, state_str(ctx->state));
            return DOMO_OK;

        default:
            resp->status = DOMO_ERR_INVALID_COMMAND;
            return DOMO_OK;
    }
}

int bulb_device_main(device_id_t id) {
    bulb_ctx_t ctx;
    domo_message_t req;
    domo_message_t resp;
    int fd;
    int dummy_fd;

    memset(&ctx, 0, sizeof(ctx));
    ctx.id = id;
    ctx.state = DOMO_STATE_OFF;

    if (domo_make_device_fifo_path(id, ctx.fifo_path, sizeof(ctx.fifo_path)) != DOMO_OK) {
        return DOMO_ERR_SYSTEM;
    }

    unlink(ctx.fifo_path);
    if (mkfifo(ctx.fifo_path, 0666) != 0 && errno != EEXIST) {
        return DOMO_ERR_SYSTEM;
    }

    signal(SIGTERM, on_sigterm);
    srand((unsigned int)(getpid() ^ id));

    fd = open(ctx.fifo_path, O_RDONLY);
    if (fd < 0) {
        unlink(ctx.fifo_path);
        return DOMO_ERR_SYSTEM;
    }

    dummy_fd = open(ctx.fifo_path, O_WRONLY | O_NONBLOCK);

    while (keep_running) {
        int rc = domo_recv_message(fd, &req);
        if (rc != DOMO_OK) {
            continue;
        }

        rc = handle_request(&ctx, &req, &resp);
        if (rc != DOMO_OK) {
            continue;
        }

        if (req.payload[0] != '\0') {
            rc = domo_send_message(req.payload, &resp);
        } else {
            char reply_fifo[DOMO_PATH_MAX];
            rc = domo_make_reply_fifo_path(req.src_pid, req.request_id, reply_fifo, sizeof(reply_fifo));
            if (rc == DOMO_OK) {
                rc = domo_send_message(reply_fifo, &resp);
            }
        }

        (void)rc;
    }

    close(fd);
    if (dummy_fd >= 0) close(dummy_fd);
    unlink(ctx.fifo_path);
    return DOMO_OK;
}