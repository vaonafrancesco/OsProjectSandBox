#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "error_codes.h"
#include "ipc.h"
#include "utils.h"

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    device_id_t id;
    device_id_t controlled_device_id;
    int begin_hour;
    int begin_minute;
    int end_hour;
    int end_minute;
    char fifo_path[DOMO_PATH_MAX];
} timer_ctx_t;

static void on_sigterm(int sig) {
    (void)sig;
    keep_running = 0;
}

static int parse_time(const char *timestr, int *hour, int *minute) {
    if (timestr == NULL || hour == NULL || minute == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    if (sscanf(timestr, "%d:%d", hour, minute) != 2) {
        return DOMO_ERR_INVALID_TIME;
    }

    if (*hour < 0 || *hour > 23 || *minute < 0 || *minute > 59) {
        return DOMO_ERR_INVALID_TIME;
    }

    return DOMO_OK;
}

static int is_within_schedule(const timer_ctx_t *ctx) {
    if (ctx == NULL) {
        return 0;
    }

    int begin_minutes = ctx->begin_hour * 60 + ctx->begin_minute;
    int end_minutes = ctx->end_hour * 60 + ctx->end_minute;

    time_t now = time(NULL);
    struct tm now_tm;
    localtime_r(&now, &now_tm);
    int current_minutes = now_tm.tm_hour * 60 + now_tm.tm_min;

    if (begin_minutes == end_minutes) {
        return 0;
    }
    if (begin_minutes < end_minutes) {
        return current_minutes >= begin_minutes && current_minutes < end_minutes;
    }
    return current_minutes >= begin_minutes || current_minutes < end_minutes;
}

static int build_info_payload(const timer_ctx_t *ctx, char *buf, size_t len) {
    if (ctx == NULL || buf == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    int active = is_within_schedule(ctx);

    snprintf(buf, len,
             "timer id=%d begin=%02d:%02d end=%02d:%02d active=%d child=%d fifo=%s",
             ctx->id,
             ctx->begin_hour,
             ctx->begin_minute,
             ctx->end_hour,
             ctx->end_minute,
             active,
             ctx->controlled_device_id,
             ctx->fifo_path);
    return DOMO_OK;
}

static int query_controlled_device(timer_ctx_t *ctx, domo_message_t *resp) {
    if (ctx == NULL || resp == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }
    if (ctx->controlled_device_id < 0) {
        resp->status = DOMO_ERR_DEVICE_NOT_FOUND;
        return DOMO_OK;
    }

    domo_message_t req;
    char device_fifo[DOMO_PATH_MAX];
    char reply_fifo[DOMO_PATH_MAX];

    if (domo_make_device_fifo_path(ctx->controlled_device_id, device_fifo, sizeof(device_fifo)) != DOMO_OK) {
        resp->status = DOMO_ERR_DEVICE_NOT_FOUND;
        return DOMO_OK;
    }

    memset(&req, 0, sizeof(req));
    req.kind = DOMO_MSG_REQUEST;
    req.cmd = DOMO_CMD_INFO;
    req.src_id = ctx->id;
    req.dst_id = ctx->controlled_device_id;
    req.src_pid = getpid();
    req.request_id = (int)(time(NULL) ^ (ctx->controlled_device_id << 3));

    if (domo_make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo)) != DOMO_OK) {
        resp->status = DOMO_ERR_IPC_FAILURE;
        return DOMO_OK;
    }

    if (domo_request_reply(device_fifo, reply_fifo, &req, resp) != DOMO_OK) {
        resp->status = DOMO_ERR_DEVICE_NOT_FOUND;
        return DOMO_OK;
    }

    return DOMO_OK;
}

static int handle_request(timer_ctx_t *ctx, const domo_message_t *req, domo_message_t *resp) {
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

    domo_simulate_delay();

    switch (req->cmd) {
        case DOMO_CMD_INFO:
            return build_info_payload(ctx, resp->payload, sizeof(resp->payload));

        case DOMO_CMD_SET_PARAM:
            if (strcmp(req->arg1, "begin") == 0) {
                int h, m;
                resp->status = parse_time(req->arg2, &h, &m);
                if (resp->status == DOMO_OK) {
                    ctx->begin_hour = h;
                    ctx->begin_minute = m;
                    snprintf(resp->payload, sizeof(resp->payload),
                             "timer %d begin=%02d:%02d",
                             ctx->id, h, m);
                }
            } else if (strcmp(req->arg1, "end") == 0) {
                int h, m;
                resp->status = parse_time(req->arg2, &h, &m);
                if (resp->status == DOMO_OK) {
                    ctx->end_hour = h;
                    ctx->end_minute = m;
                    snprintf(resp->payload, sizeof(resp->payload),
                             "timer %d end=%02d:%02d",
                             ctx->id, h, m);
                }
            } else {
                resp->status = DOMO_ERR_INVALID_PARAMETERS;
            }
            return DOMO_OK;

        case DOMO_CMD_ADD_CHILD:
            ctx->controlled_device_id = (device_id_t)atoi(req->arg1);
            snprintf(resp->payload, sizeof(resp->payload),
                     "timer %d controls device %d",
                     ctx->id, ctx->controlled_device_id);
            return DOMO_OK;

        case DOMO_CMD_REMOVE_CHILD:
            if (ctx->controlled_device_id == (device_id_t)atoi(req->arg1)) {
                ctx->controlled_device_id = -1;
                snprintf(resp->payload, sizeof(resp->payload),
                         "timer %d removed child %d",
                         ctx->id, req->arg1[0] ? atoi(req->arg1) : -1);
            } else {
                resp->status = DOMO_ERR_DEVICE_NOT_FOUND;
            }
            return DOMO_OK;

        case DOMO_CMD_GET_STATE:
            return query_controlled_device(ctx, resp);

        default:
            resp->status = DOMO_ERR_INVALID_COMMAND;
            return DOMO_OK;
    }
}

int timer_device_main(device_id_t id) {
    timer_ctx_t ctx;
    domo_message_t req;
    domo_message_t resp;
    int fd;
    int dummy_fd;

    memset(&ctx, 0, sizeof(ctx));
    ctx.id = id;
    ctx.controlled_device_id = -1;
    ctx.begin_hour = 0;
    ctx.begin_minute = 0;
    ctx.end_hour = 0;
    ctx.end_minute = 0;

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
            domo_send_message(req.payload, &resp);
        } else {
            char reply_fifo[DOMO_PATH_MAX];
            if (domo_make_reply_fifo_path(req.src_pid, req.request_id, reply_fifo, sizeof(reply_fifo)) == DOMO_OK) {
                domo_send_message(reply_fifo, &resp);
            }
        }
    }

    close(fd);
    if (dummy_fd >= 0) {
        close(dummy_fd);
    }
    unlink(ctx.fifo_path);
    return DOMO_OK;
}
