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

#define HUB_MAX_CHILDREN 32

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    device_id_t id;
    domo_state_t state;
    int manual_override;
    int child_count;
    device_id_t child_ids[HUB_MAX_CHILDREN];
    char fifo_path[DOMO_PATH_MAX];
} hub_ctx_t;

static void on_sigterm(int sig) {
    (void)sig;
    keep_running = 0;
}

static const char *state_str(domo_state_t state) {
    switch (state) {
        case DOMO_STATE_ON: return "on";
        case DOMO_STATE_OFF: return "off";
        case DOMO_STATE_OPEN: return "open";
        case DOMO_STATE_CLOSED: return "closed";
        case DOMO_STATE_MANUAL_OVERRIDE: return "manual_override";
        default: return "unknown";
    }
}

static int parse_state_from_payload(const char *payload, domo_state_t *out_state) {
    if (payload == NULL || out_state == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    if (strstr(payload, "state=on") != NULL) {
        *out_state = DOMO_STATE_ON;
        return DOMO_OK;
    }
    if (strstr(payload, "state=off") != NULL) {
        *out_state = DOMO_STATE_OFF;
        return DOMO_OK;
    }
    if (strstr(payload, "state=open") != NULL) {
        *out_state = DOMO_STATE_OPEN;
        return DOMO_OK;
    }
    if (strstr(payload, "state=closed") != NULL) {
        *out_state = DOMO_STATE_CLOSED;
        return DOMO_OK;
    }

    *out_state = DOMO_STATE_UNKNOWN;
    return DOMO_ERR_INVALID_STATE;
}

static int add_child(hub_ctx_t *ctx, device_id_t child_id) {
    if (ctx == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }
    if (ctx->child_count >= HUB_MAX_CHILDREN) {
        return DOMO_ERR_NOT_ALLOWED;
    }
    ctx->child_ids[ctx->child_count++] = child_id;
    return DOMO_OK;
}

static int remove_child(hub_ctx_t *ctx, device_id_t child_id) {
    if (ctx == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }
    for (int i = 0; i < ctx->child_count; ++i) {
        if (ctx->child_ids[i] == child_id) {
            for (int j = i; j < ctx->child_count - 1; ++j) {
                ctx->child_ids[j] = ctx->child_ids[j + 1];
            }
            ctx->child_count--;
            return DOMO_OK;
        }
    }
    return DOMO_ERR_DEVICE_NOT_FOUND;
}

static int query_children_state(hub_ctx_t *ctx, domo_state_t *out_state, int *out_override, int *out_failed) {
    domo_state_t first_state = DOMO_STATE_UNKNOWN;
    int manual_override = 0;
    int had_failed = 0;

    if (ctx == NULL || out_state == NULL || out_override == NULL || out_failed == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    if (ctx->child_count == 0) {
        *out_state = DOMO_STATE_OFF;
        *out_override = 0;
        *out_failed = 0;
        return DOMO_OK;
    }

    for (int i = 0; i < ctx->child_count; ++i) {
        device_id_t child_id = ctx->child_ids[i];
        domo_message_t req;
        domo_message_t resp;
        char child_fifo[DOMO_PATH_MAX];
        char reply_fifo[DOMO_PATH_MAX];
        domo_state_t child_state;

        if (domo_make_device_fifo_path(child_id, child_fifo, sizeof(child_fifo)) != DOMO_OK) {
            had_failed = 1;
            continue;
        }

        memset(&req, 0, sizeof(req));
        req.kind = DOMO_MSG_REQUEST;
        req.cmd = DOMO_CMD_INFO;
        req.src_id = ctx->id;
        req.dst_id = child_id;
        req.src_pid = getpid();
        req.request_id = (int)(time(NULL) ^ (child_id << 3));

        if (domo_make_reply_fifo_path(getpid(), req.request_id, reply_fifo, sizeof(reply_fifo)) != DOMO_OK) {
            had_failed = 1;
            continue;
        }

        if (domo_request_reply(child_fifo, reply_fifo, &req, &resp) != DOMO_OK) {
            had_failed = 1;
            continue;
        }

        if (parse_state_from_payload(resp.payload, &child_state) != DOMO_OK) {
            had_failed = 1;
            continue;
        }

        if (first_state == DOMO_STATE_UNKNOWN) {
            first_state = child_state;
        } else if (first_state != child_state) {
            manual_override = 1;
        }
    }

    if (first_state == DOMO_STATE_UNKNOWN) {
        *out_state = DOMO_STATE_UNKNOWN;
    } else if (manual_override) {
        *out_state = DOMO_STATE_MANUAL_OVERRIDE;
    } else {
        *out_state = first_state;
    }

    *out_override = manual_override;
    *out_failed = had_failed;
    return DOMO_OK;
}

static int propagate_switch_to_children(hub_ctx_t *ctx, const domo_message_t *req) {
    int failures = 0;

    if (ctx == NULL || req == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    for (int i = 0; i < ctx->child_count; ++i) {
        device_id_t child_id = ctx->child_ids[i];
        domo_message_t child_req;
        domo_message_t child_resp;
        char child_fifo[DOMO_PATH_MAX];
        char reply_fifo[DOMO_PATH_MAX];

        if (domo_make_device_fifo_path(child_id, child_fifo, sizeof(child_fifo)) != DOMO_OK) {
            failures++;
            continue;
        }

        memset(&child_req, 0, sizeof(child_req));
        child_req.kind = DOMO_MSG_REQUEST;
        child_req.cmd = DOMO_CMD_SWITCH;
        child_req.src_id = ctx->id;
        child_req.dst_id = child_id;
        child_req.src_pid = getpid();
        child_req.request_id = (int)(time(NULL) ^ (child_id << 3));
        strncpy(child_req.arg1, req->arg1, sizeof(child_req.arg1) - 1);
        strncpy(child_req.arg2, req->arg2, sizeof(child_req.arg2) - 1);

        if (domo_make_reply_fifo_path(getpid(), child_req.request_id, reply_fifo, sizeof(reply_fifo)) != DOMO_OK) {
            failures++;
            continue;
        }

        if (domo_request_reply(child_fifo, reply_fifo, &child_req, &child_resp) != DOMO_OK) {
            failures++;
        }
    }

    ctx->manual_override = 0;
    return failures > 0 ? DOMO_ERR_CHILD_CRASHED : DOMO_OK;
}

static int build_info_payload(hub_ctx_t *ctx, char *buf, size_t len) {
    if (ctx == NULL || buf == NULL) {
        return DOMO_ERR_INVALID_PARAMETERS;
    }

    domo_state_t state;
    int override = 0;
    int failed = 0;

    query_children_state(ctx, &state, &override, &failed);
    ctx->state = state;
    ctx->manual_override = override;

    snprintf(buf, len,
             "hub id=%d state=%s children=%d override=%d fifo=%s",
             ctx->id,
             state_str(ctx->state),
             ctx->child_count,
             ctx->manual_override,
             ctx->fifo_path);
    return DOMO_OK;
}

static int handle_request(hub_ctx_t *ctx, const domo_message_t *req, domo_message_t *resp) {
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

        case DOMO_CMD_SWITCH:
            resp->status = propagate_switch_to_children(ctx, req);
            if (resp->status == DOMO_OK) {
                snprintf(resp->payload, sizeof(resp->payload),
                         "hub %d propagated switch to %d children",
                         ctx->id, ctx->child_count);
            } else {
                snprintf(resp->payload, sizeof(resp->payload),
                         "hub %d switch propagated with errors",
                         ctx->id);
            }
            return DOMO_OK;

        case DOMO_CMD_ADD_CHILD:
            {
                device_id_t child_id = (device_id_t)atoi(req->arg1);
                resp->status = add_child(ctx, child_id);
                if (resp->status == DOMO_OK) {
                    snprintf(resp->payload, sizeof(resp->payload),
                             "hub %d added child %d",
                             ctx->id, child_id);
                }
            }
            return DOMO_OK;

        case DOMO_CMD_REMOVE_CHILD:
            {
                device_id_t child_id = (device_id_t)atoi(req->arg1);
                resp->status = remove_child(ctx, child_id);
                if (resp->status == DOMO_OK) {
                    snprintf(resp->payload, sizeof(resp->payload),
                             "hub %d removed child %d",
                             ctx->id, child_id);
                }
            }
            return DOMO_OK;

        default:
            resp->status = DOMO_ERR_INVALID_COMMAND;
            return DOMO_OK;
    }
}

int hub_device_main(device_id_t id) {
    hub_ctx_t ctx;
    domo_message_t req;
    domo_message_t resp;
    int fd;
    int dummy_fd;

    memset(&ctx, 0, sizeof(ctx));
    ctx.id = id;
    ctx.state = DOMO_STATE_OFF;
    ctx.manual_override = 0;
    ctx.child_count = 0;

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
