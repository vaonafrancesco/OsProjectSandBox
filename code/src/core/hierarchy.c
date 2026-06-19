#include <stdio.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/routing.h"
#include "../../include/error_codes.h"

extern routing_node routing_table[MAX_DEVICES];

bool is_control_device(device_type type)
{
    return (type == DEVICE_CONTROLLER ||
            type == DEVICE_HUB ||
            type == DEVICE_TIMER);
}

static int get_node_index(int id)
{
    int i;

    for (i = 0; i < MAX_DEVICES; i++) {
        if (routing_table[i].id == id) {
            return i;
        }
    }

    return -1;
}

int routing_link_devices(int child_id, int parent_id)
{
    int child_idx;
    int parent_idx;
    int current_id;
    int safety = 0;

    if (child_id < 0 || parent_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

    if (child_id == parent_id) {
        return ERR_SELF_LINK;
    }

    child_idx = get_node_index(child_id);
    if (child_idx == -1) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if (parent_id == CONTROLLER_ID) {
        routing_table[child_idx].parent_id = CONTROLLER_ID;
        return OK;
    }

    parent_idx = get_node_index(parent_id);
    if (parent_idx == -1) {
        return ERR_DEVICE_NOT_FOUND;
    }

    if (!is_control_device(routing_table[parent_idx].type)) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    current_id = parent_id;

    while (1) {
        int current_idx;

        if (current_id == child_id) {
            return ERR_CYCLE_DETECTED;
        }

        if (current_id == CONTROLLER_ID) {
            break;
        }

        current_idx = get_node_index(current_id);
        if (current_idx == -1) {
            return ERR_INVALID_STATE;
        }

        current_id = routing_table[current_idx].parent_id;

        safety++;
        if (safety > MAX_DEVICES) {
            return ERR_CYCLE_DETECTED;
        }
    }

    routing_table[child_idx].parent_id = parent_id;
    return OK;
}