#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "controller.h"
#include "common.h"
#include "device.h"
#include "error_codes.h"
#include "parser.h"


/*	Convert a string argument from the command line into a numeric device ID.
	Uses strtol to catch invalid characters and ensures the ID is not negative. */
static int parse_device_id_arg(const char *text, device_id *out_id){
    char*endptr = NULL;
    long value;

    if(text==NULL || out_id ==NULL || *text == '\0'){
        return ERR_INVALID_PARAMETERS;
    }

    value = strtol(text, &endptr,10);
    // If there are leftover characters or the number is negative, it's an error
    if(*endptr!='\0' || value <0){
        return ERR_INVALID_PARAMETERS;
    }

    *out_id = (device_id )value;
    return OK;
}

// Check the string input and match it to our device type enum
static int parse_device_type_arg(const char *text, device_type *out_type){
    if(text==NULL || out_type ==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(strcmp(text, "hub")==0){
        *out_type = DEVICE_HUB;
        return OK;
    }
    if(strcmp(text, "timer")==0){
        *out_type = DEVICE_TIMER;
        return OK;
    }
    if(strcmp(text, "bulb")==0){
        *out_type = DEVICE_BULB;
        return OK;
    }
    if(strcmp(text, "window")==0){
        *out_type = DEVICE_WINDOW;
        return OK;
    }
    if(strcmp(text, "fridge")==0){
        *out_type = DEVICE_FRIDGE;
        return OK;
    }
    return ERR_DEVICE_TYPE_MISMATCH; // Unknown device type
}

/*	Make sure the child device actually exists in our array.
	Also prevents the main controller from being treated as a child. */
static int validate_child_exists(const controller *ctrl, device_id child_id){
    if(ctrl==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(child_id == CONTROLLER_ID){
        return ERR_NOT_ALLOWED;
    }
    if(controller_find_device_const(ctrl, child_id)==NULL){
        return ERR_DEVICE_NOT_FOUND;
    }
    return OK;
}

// Make sure the target parent exists and is capable of having children 
static int validate_parent_for_link(const controller *ctrl, device_id parent_id){
    const device *parent_entry;

    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }
	
	// The main controller can always be a parent
    if(parent_id == CONTROLLER_ID){
        return OK;
    }

    parent_entry = controller_find_device_const(ctrl, parent_id);
    if(parent_entry == NULL){
        return ERR_DEVICE_NOT_FOUND;
    }
	
	// Check if the device type allows having children
    if(!device_is_control(parent_entry->info.type)){
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    return OK;
}

// Wrapper function to check if a link operation is valid before doing it
static int validate_link_request(const controller *ctrl, device_id child_id, device_id parent_id){
    int rc;
    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(child_id == parent_id){
        return ERR_SELF_LINK;
    }
    // Self linking mitigation
    rc=validate_child_exists(ctrl, child_id);
    if(rc!=OK){
        return rc;
    }
    rc = validate_parent_for_link(ctrl, parent_id);
    if(rc!=OK){
        return rc;
    }

    return OK;
}

// Handler for the 'list' command
int cmd_list(controller *ctrl, const parsed_command *cmd){
    (void)cmd;	// Suppress unused variable warning
    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    return controller_list_devices(ctrl);
}

// Handler for the 'add <type>' command
int cmd_add(controller *ctrl, const parsed_command *cmd){
    device_type type;
    int rc;

    if(ctrl==NULL || cmd==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    // 'add' needs exactly 1 argument (the device type)
    if(cmd->argc !=1){
        return ERR_INVALID_PARAMETERS;
    }

    rc = parse_device_type_arg(cmd->argv[0], &type);
    if(rc!= OK){
        return rc;
    }
	// Can't add another main controller dynamically
    if(type ==DEVICE_CONTROLLER || type == DEVICE_UNKNOWN){
        return ERR_DEVICE_TYPE_MISMATCH;
    }
    return controller_add_device(ctrl, type);
}

// Handler for the 'del <id>' command
int cmd_del(controller *ctrl, const parsed_command *cmd){
    device_id id;
    int rc;

    if(ctrl==NULL || cmd ==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(cmd->argc!=1){
        return ERR_INVALID_PARAMETERS;
    }
    rc =parse_device_id_arg(cmd->argv[0], &id);
    if(rc!=OK){
        return rc;
    }
    // Prevent the user from deleting the main controller itself
    if(id==CONTROLLER_ID){
        return ERR_NOT_ALLOWED;
    }

    return controller_delete_device(ctrl, id);
}

// Handler for the 'info <id>' command
int cmd_info(controller *ctrl, const parsed_command *cmd){
    device_id id;
    int rc;
    if(ctrl==NULL || cmd == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(cmd->argc!=1){
        return ERR_INVALID_PARAMETERS;
    }
    
    rc=parse_device_id_arg(cmd->argv[0], &id);
    if(rc!=OK){
        return rc;
    }
    return controller_info_device(ctrl, id);
}

// Handler for the 'switch <id> <label> <pos>' command
int cmd_switch(controller *ctrl, const parsed_command *cmd){
    device_id id;
    int rc;
    if(ctrl==NULL || cmd==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(cmd->argc!=3){
        return ERR_INVALID_PARAMETERS;
    }

    rc =parse_device_id_arg(cmd->argv[0], &id);
    if(rc!=OK){
        return rc;
    }
	
	// Check if the label or position strings are empty
    if(cmd->argv[1][0]=='\0'|| cmd->argv[2][0]=='\0'){
        return ERR_INVALID_PARAMETERS;
    }

    return controller_switch_device(ctrl, id, cmd->argv[1], cmd->argv[2]);
}

// Handler for the 'link <child> <parent>' command
int cmd_link(controller *ctrl, const parsed_command *cmd){
    device_id child_id;
    device_id parent_id;
    int rc;

    if(ctrl==NULL || cmd ==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(cmd->argc!=2){
        return ERR_INVALID_PARAMETERS;
    }

    rc =parse_device_id_arg(cmd->argv[0], &child_id);
    if(rc!=OK){
        return rc;
    }
    rc =parse_device_id_arg(cmd->argv[1], &parent_id);
    if(rc!=OK){
        return rc;
    }
    // Check if the link is legal
    rc=validate_link_request(ctrl, child_id,parent_id);
    if(rc!=OK){
        return rc;
    }
    // Actually link the devices
    rc=controller_link_devices(ctrl, child_id, parent_id);
    if(rc!=OK){
        return ERR_LINK_FAILED;
    }
    return OK;
}

// Print the list of available commands to the user
int cmd_help(void){
    puts("Avaiable commands:");
    puts(" list");
    puts(" add <hub|timer|bulb|window|fridge>");
    puts(" del <id>");
    puts(" info <id>");
    puts(" link <child_id> to <parent_id>");
    puts(" switch <id> <label> <pos>");
    puts(" help");
    puts(" exit");
    return OK;
}

/*	Main dispatcher function: takes the command parsed from the user input
	and calls the correct handler function using a switch-case. */
int execute_parsed_command(controller *ctrl, const parsed_command *cmd){
    if(ctrl==NULL || cmd ==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    switch(cmd->type){
        case PARSER_CMD_LIST:
            return cmd_list(ctrl, cmd);
        case PARSER_CMD_ADD:
            return cmd_add(ctrl, cmd);
        case PARSER_CMD_DEL:
            return cmd_del(ctrl, cmd);
        case PARSER_CMD_INFO:
            return cmd_info(ctrl, cmd);
        case PARSER_CMD_LINK:
            return cmd_link(ctrl, cmd);
        case PARSER_CMD_SWITCH:
            return cmd_switch(ctrl, cmd);
        case PARSER_CMD_HELP:
            return cmd_help();
        case PARSER_CMD_EXIT:
            ctrl->running=0;	// Stop the main loop
            return OK;
        case PARSER_CMD_INVALID:
        default:
            return ERR_INVALID_COMMAND;
    }
}
