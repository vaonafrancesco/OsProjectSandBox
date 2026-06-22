#include <stdio.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/routing.h"
#include "../../include/error_codes.h"

extern routing_node routing_table[MAX_DEVICES];

//	Check if a device is a "boss" type that is allowed to have children. (hub: y; bulb: no)
bool is_control_device(device_type type)
{
    return (type == DEVICE_CONTROLLER ||
            type == DEVICE_HUB ||
            type == DEVICE_TIMER);
}

/*	Quick helper function to scan the routing array and find the exact index of a device.
	Returns -1 if the device isn't in the list. */	
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

/*	The main function to connect a child device to a new parent device.
	It does a bunch of safety checks before actually changing the array. */
int routing_link_devices(int child_id, int parent_id)
{
    int child_idx;
    int parent_idx;
    int current_id;
    int safety = 0;
	
	// Check 1: Basic validation. Don't pass negative IDs.
    if (child_id < 0 || parent_id < 0) {
        return ERR_INVALID_PARAMETERS;
    }

	// Check 2: A device cannot be its own parent!
    if (child_id == parent_id) {
        return ERR_SELF_LINK;
    }

	// Check 3: Make sure the child actually exists in our table.
    child_idx = get_node_index(child_id);
    if (child_idx == -1) {
        return ERR_DEVICE_NOT_FOUND;
    }
    
	// If the new parent is the main controller, we can just link it directly.
    if (parent_id == CONTROLLER_ID) {
        routing_table[child_idx].parent_id = CONTROLLER_ID;
        return OK;
    }
	
	// Check 4: Make sure the intended parent actually exists.
    parent_idx = get_node_index(parent_id);
    if (parent_idx == -1) {
        return ERR_DEVICE_NOT_FOUND;
    }
	
	// Check 5: Ensure the parent is a control device (hub/timer), not a leaf node like a bulb.
    if (!is_control_device(routing_table[parent_idx].type)) {
        return ERR_DEVICE_TYPE_MISMATCH;
    }
	
	// Check 6: CYCLE DETECTION
	// We need to make sure we don't accidentally create an infinite loop
    current_id = parent_id;
	
	// Walk up the tree from the parent to the top.
    while (1) {
        int current_idx;
		
		// Linking them now would create a circular dependency.
        if (current_id == child_id) {
            return ERR_CYCLE_DETECTED;
        }
		
		// We reached the top of the tree safely. No cycle detected
        if (current_id == CONTROLLER_ID) {
            break;
        }
        
		// Find the next ancestor in the chain
        current_idx = get_node_index(current_id);
        if (current_idx == -1) {
            return ERR_INVALID_STATE;
        }

        current_id = routing_table[current_idx].parent_id;
		
		/*	Safety net: if the tree is somehow deeper than the max number of devices, 
        	we are already stuck in an infinite loop -> Break out */
        safety++;
        if (safety > MAX_DEVICES) {
            return ERR_CYCLE_DETECTED;
        }
    }
	
	// All safety checks passed, update the child's parent ID
    routing_table[child_idx].parent_id = parent_id;
    return OK;
}
