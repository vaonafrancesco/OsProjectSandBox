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
#include "utils.h"

static volatile sig_atomic_t keep_running = 1;

typedef struct {
    device_id_t id;
    domo_state_t state;

    unsigned long open_time;
    time_t open_since;

    char fifo_path[DOMO_PATH_MAX];
} window_ctx_t;


static void on_sigterm(int sig)
{
    (void)sig;
    keep_running = 0;
}


/* usata solo per stampare lo stato */
static const char *state_str(domo_state_t state)
{
    switch (state) {

        case DOMO_STATE_OPEN:
            return "open";

        case DOMO_STATE_CLOSED:
            return "closed";

        default:
            break;
    }

    return "unknown";
}


/*
 * aggiorna il tempo totale in cui la finestra è rimasta aperta
 */
static void update_open_time(window_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->state == DOMO_STATE_OPEN && ctx->open_since != 0) {

        time_t now = time(NULL);

        ctx->open_time += (unsigned long)(now - ctx->open_since);

        /* resetto il timer */
        ctx->open_since = now;
    }
}


static int build_info_payload(const window_ctx_t *ctx,
                              char *buf,
                              size_t len)
{
    window_ctx_t tmp;

    if (ctx == NULL || buf == NULL)
        return DOMO_ERR_INVALID_PARAMETERS;

    memset(&tmp, 0, sizeof(tmp));
    tmp = *ctx;

    /*
     * se è ancora aperta aggiorno temporaneamente
     * il tempo per mostrarlo nella info
     */
    if (tmp.state == DOMO_STATE_OPEN) {

        if (tmp.open_since != 0) {

            time_t now = time(NULL);

            tmp.open_time +=
                (unsigned long)(now - tmp.open_since);
        }
    }

    snprintf(
        buf,
        len,
        "window id=%d state=%s time=%lu fifo=%s",
        tmp.id,
        state_str(tmp.state),
        tmp.open_time,
        tmp.fifo_path
    );

    return DOMO_OK;
}


static int handle_request(window_ctx_t *ctx,
                          const domo_message_t *req,
                          domo_message_t *resp)
{
    if (ctx == NULL || req == NULL || resp == NULL)
        return DOMO_ERR_INVALID_PARAMETERS;

    memset(resp, 0, sizeof(*resp));

    resp->kind = DOMO_MSG_RESPONSE;
    resp->cmd = req->cmd;

    resp->src_id = ctx->id;
    resp->dst_id = req->src_id;

    resp->src_pid = getpid();

    resp->request_id = req->request_id;
    resp->status = DOMO_OK;

    /* simula il comportamento del dispositivo */
    domo_simulate_delay();

    switch (req->cmd) {

        case DOMO_CMD_INFO:

            build_info_payload(
                ctx,
                resp->payload,
                sizeof(resp->payload)
            );

            return DOMO_OK;


        case DOMO_CMD_SWITCH:

            if (strcmp(req->arg1, "open") == 0) {

                if (ctx->state != DOMO_STATE_OPEN) {

                    ctx->state = DOMO_STATE_OPEN;
                    ctx->open_since = time(NULL);
                }

                snprintf(
                    resp->payload,
                    sizeof(resp->payload),
                    "window %d opened",
                    ctx->id
                );
            }

            else if (strcmp(req->arg1, "close") == 0) {

                if (ctx->state == DOMO_STATE_OPEN)
                    update_open_time(ctx);

                ctx->state = DOMO_STATE_CLOSED;
                ctx->open_since = 0;

                snprintf(
                    resp->payload,
                    sizeof(resp->payload),
                    "window %d closed",
                    ctx->id
                );
            }

            else {

                resp->status = DOMO_ERR_INVALID_PARAMETERS;
            }

            return DOMO_OK;


        default:

            resp->status = DOMO_ERR_INVALID_COMMAND;
            break;
    }

    return DOMO_OK;
}


int window_device_main(device_id_t id)
{
    window_ctx_t ctx;

    domo_message_t req;
    domo_message_t resp;

    int fd;
    int dummy_fd;


    memset(&ctx, 0, sizeof(window_ctx_t));

    ctx.id = id;
    ctx.state = DOMO_STATE_CLOSED;

    ctx.open_time = 0;
    ctx.open_since = 0;


    if (domo_make_device_fifo_path(
            id,
            ctx.fifo_path,
            sizeof(ctx.fifo_path)
        ) != DOMO_OK)
    {
        return DOMO_ERR_SYSTEM;
    }


    unlink(ctx.fifo_path);

    if (mkfifo(ctx.fifo_path, 0666) != 0) {

        if (errno != EEXIST)
            return DOMO_ERR_SYSTEM;
    }


    signal(SIGTERM, on_sigterm);

    srand((unsigned int)(getpid() ^ id));


    fd = open(ctx.fifo_path, O_RDONLY);

    if (fd < 0) {

        unlink(ctx.fifo_path);
        return DOMO_ERR_SYSTEM;
    }


    /*
     * fd fittizio per evitare EOF
     * quando nessuno scrive nella fifo
     */
    dummy_fd = open(
        ctx.fifo_path,
        O_WRONLY | O_NONBLOCK
    );


    while (keep_running) {

        int rc;

        rc = domo_recv_message(fd, &req);

        if (rc != DOMO_OK) {

            /* ignoro messaggi non validi */
            continue;
        }


        rc = handle_request(&ctx, &req, &resp);

        if (rc != DOMO_OK)
            continue;


        if (req.payload[0] != '\0') {

            domo_send_message(req.payload, &resp);
        }

        else {

            char reply_fifo[DOMO_PATH_MAX];

            rc = domo_make_reply_fifo_path(
                req.src_pid,
                req.request_id,
                reply_fifo,
                sizeof(reply_fifo)
            );

            if (rc == DOMO_OK)
                domo_send_message(reply_fifo, &resp);
        }
    }


    close(fd);

    if (dummy_fd >= 0)
        close(dummy_fd);

    unlink(ctx.fifo_path);

    return DOMO_OK;
}