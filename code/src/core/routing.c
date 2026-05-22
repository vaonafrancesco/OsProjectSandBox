#include <stdio.h>
#include <string.h>

#include "../../include/protocol.h"
#include "../../include/routing.h"

//Global routing table
//declared here but accessed by hierarchy.c via 'extern'
routing_node routing_table[MAX_DEVICES];

//routing_init
//initializes the routing table with all the slots set to empty (-1)

void routing_init(void){
    for (int i=0; i<MAX_DEVICES; i++){
    routing_table[i].id = -1; // -1 =empty slot
    routing_table[i].parent_id = -1;
    routing_table[i].type = DEV_UNKNOWN;
    }
}

//routing_add_node
//Register a new device in the system with its parent as default setted to 0.

int routing_add_node(int id, device_type type){
    if (id<0)return IPC_ERROR;

        for (int i = 0; i< MAX_DEVICES; i++){
		if (routing_table[i].id == -1){
			routing_table[i].id = id;
			routing_table[i].parent_id = CONTROLLER_ID; // Always defaults to 0
			routing_table[i].type = type;
			return OK;
		}
	}
	return IPC_ERROR; // Routing table is full
}

//routing_remove_node
//removes a device from the routing table (cascading deletion of children is handled at the IPC/CONTROLLER level.

int routing_remove_node(int id){
    int found = 0;

        for (int i =0; i< MAX_DEVICES; i++){
	    	if (routing_table[i].id == id){
		    	routing_table[i].id = -1;	//Mark slots as deleted
			    routing_table[i].parent_id = -1;
			    routing_table[i].type = DEV_UNKNOWN;
		    	found = 1;
			    break;
		    }
	    }
	
	return found ? OK : DEVICE_NOT_FOUND;
}
