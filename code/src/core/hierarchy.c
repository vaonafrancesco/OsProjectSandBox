#include <stdio.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/routing.h"
#include "../../include/error_codes.h"

// Access the global table defined and initialized in routing.cex
extern routing_node routing_table[MAX_DEVICES];

//is_control_device
//VAlidates if a device type is allowed to have children

bool is_control_device(device_type type){
    return (type == DEVICE_CONTROLLER || type == DEVICE_HUB || type == DEVICE_TIMER);
}

//get_node_index (Private Helper)
//finds the array index of a device by its logical ID.

static int get_node_index(int id){
    for (int i=0; i<MAX_DEVICES; i++){
        if (routing_table[i].id == id){
            return i;
        }
    }
    return -1; // DEvice not found in the array
}

//routing_link_devices
//Tries to logically link child_id to parent_id.
//Enforces tree structure by checking for DEVICE_TYPE_MISMATCH and CYCLE_DETECTED (preventing infinite loops)

int routing_link_devices(int child_id, int parent_id){
    // you can't link a device to itself
    if (child_id == parent_id){
        return ERR_SELF_LINK;
    }
    int child_idx = get_node_index(child_id);
	int parent_idx = get_node_index(parent_id);
	
	// Ensure both devices actually exist in the registry
	if (child_idx == -1 || parent_idx == -1){
		return ERR_DEVICE_NOT_FOUND;
	}
	
	// Validate Parent type (only Controller, Hub and Timer can be parents)
	if (!is_control_device(routing_table[parent_idx].type)){
		return ERR_DEVICE_TYPE_MISMATCH;
	}
	
	// Cycle Detection Algorithm
	// We traverse upwards from the PROPOSED NEW parent up to the Controller
	// If we encounter the child_id during this operation, it means that the new parent is a descendant of the child.
	// linking them would create an infinite loop, making the IPC crash
	
	int current_ancestor_id = parent_id;
	
	while (current_ancestor_id != CONTROLLER_ID){
		
		if(current_ancestor_id == child_id) {
			return ERR_CYCLE_DETECTED;
		}
		
		int ancestor_idx = get_node_index(current_ancestor_id);
		if (ancestor_idx == -1){
			break; //safety fallback: broken chain
		}
		
		// move up one level in the logical tree
		current_ancestor_id = routing_table[ancestor_idx].parent_id;
		
	}
	
	// All checks passed. Apply the logical link safely.
    
	routing_table[child_idx].parent_id = parent_id;
	return OK;

}
