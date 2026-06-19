#ifndef ROUTING_H
#define ROUTING_H

#include <stddef.h>
#include <sys/types.h>

#include "device.h"
#include "ipc.h"

#ifndef MAX_DEVICES
#define MAX_DEVICES 128
#endif

typedef struct {
    int id;
    int parent_id;
    device_type type;
} routing_node;

extern routing_node routing_table[MAX_DEVICES];

void routing_init(void);
int routing_add_node(int id, device_type type);
int routing_remove_node(int id);
int routing_get_parent_id(int id, int *parent_id_out);
int routing_collect_children(int parent_id, device_id *children_out, int max_children, int *count_out);
int routing_link_devices(int child_id, int parent_id);

int make_reply_fifo_path(pid_t pid, int request_id, char *path, size_t path_len);
int make_device_fifo_path(device_id id, char *path, size_t path_len);

int send_message_to_fifo(const char *fifo_path, const domo_message *msg);

int request_reply_timeout(const char *target_fifo, const char *reply_fifo,
                          const domo_message *request, domo_message *response,
                          int timeout_sec);

int request_reply(const char *target_fifo, const char *reply_fifo,
                  const domo_message *request, domo_message *response);

#endif