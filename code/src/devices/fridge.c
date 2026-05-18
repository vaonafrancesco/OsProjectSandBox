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
    domo_state_t state;

    unsigned long open_time;
    time_t open_since;

    int auto_close_delay;
    int fill_percentage;

    int temperature;
    int thermostat_target;

    char fifo_path[DOMO_PATH_MAX];

} fridge_ctx_t;


static void on_sigterm(int sig)
{
    (void)sig;
    keep_running = 0;
}


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


/* aggiorna il tempo totale che è stato aperto */
static void update_open_time(fridge_ctx_t *ctx)
{
    if (ctx == NULL)
        return;

    if (ctx->state == DOMO_STATE_OPEN &&
        ctx->open_since != 0)
    {
        time_t now = time(NULL);

        ctx->open_time +=
            (unsigned long)(now - ctx->open_since);

        ctx->open_since = now;
    }
}


static int build_info_payload(const fridge_ctx_t *ctx,
                              char *buf,
                              size_t len)
{
    fridge_ctx_t snapshot;

    if (ctx == NULL || buf == NULL)
        return DOMO_ERR_INVALID_PARAMETERS;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot = *ctx;

    /*
     * aggiorno il tempo solo nella copia
     * così non modifico il contesto reale
     */
    if (snapshot.state == DOMO_STATE_OPEN) {

        if (snapshot.open_since != 0) {

            time_t now = time(NULL);

            snapshot.open_time +=
                (unsigned long)(now - snapshot.open_since);
        }
    }

    snprintf(
        buf,
        len,
        "fridge id=%d state=%s time=%lu delay=%d perc=%d temp=%d thermostat=%d fifo=%s",

        snapshot.id,
        state_str(snapshot.state),

        snapshot.open_time,

        snapshot.auto_close_delay,
        snapshot.fill_percentage,

        snapshot.temperature,
        snapshot.thermostat_target,

        snapshot.fifo_path
    );

    return DOMO_OK;
}


static int handle_request(fridge_ctx_t *ctx,
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
                    "fridge %d opened",
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
                    "fridge %d closed",
                    ctx->id
                );
            }

            else {

                resp->status = DOMO_ERR_INVALID_PARAMETERS;
            }

            return DOMO_OK;


        case DOMO_CMD_SET_PARAM:

            if (strcmp(req->arg1, "delay") == 0) {

                int value = atoi(req->arg2);

                if (value < 0) {

                    resp->status =
                        DOMO_ERR_INVALID_PARAMETERS;
                }

                else {

                    ctx->auto_close_delay = value;

                    snprintf(
                        resp->payload,
                        sizeof(resp->payload),
                        "fridge %d delay=%d",
                        ctx->id,
                        ctx->auto_close_delay
                    );
                }
            }

            else if (strcmp(req->arg1, "perc") == 0) {

                int value = atoi(req->arg2);

                if (value < 0 || value > 100) {

                    resp->status =
                        DOMO_ERR_INVALID_PARAMETERS;
                }

                else {

                    ctx->fill_percentage = value;

                    snprintf(
                        resp->payload,
                        sizeof(resp->payload),
                        "fridge %d perc=%d",
                        ctx->id,
                        ctx->fill_percentage
                    );
                }
            }

            else if (strcmp(req->arg1, "thermostat") == 0) {

                int value = atoi(req->arg2);

                ctx->thermostat_target = value;

                snprintf(
                    resp->payload,
                    sizeof(resp->payload),
                    "fridge %d thermostat=%d",
                    ctx->id,
                    ctx->thermostat_target
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


int fridge_device_main(device_id_t id)
{
    fridge_ctx_t ctx;

    domo_message_t req;
    domo_message_t resp;

    int fd;
    int dummy_fd;


    memset(&ctx, 0, sizeof(ctx));

    ctx.id = id;
    ctx.state = DOMO_STATE_CLOSED;

    ctx.open_time = 0;
    ctx.open_since = 0;

    ctx.auto_close_delay = 30;
    ctx.fill_percentage = 50;

    ctx.temperature = 5;
    ctx.thermostat_target = 4;


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
     * evita EOF continuo sulla fifo
     * quando non ci sono writer
     */
    dummy_fd = open(
        ctx.fifo_path,
        O_WRONLY | O_NONBLOCK
    );


    while (keep_running) {

        int rc;

        rc = domo_recv_message(fd, &req);

        if (rc != DOMO_OK) {

            /* messaggio ignorato */
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