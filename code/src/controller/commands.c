#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "controller.h"
#include "common.h"
#include "device.h"
#include "error_codes.h"
#include "parser.h"

static int parse_device_id_arg(const char *text, device_id *out_id){
    char*endptr = NULL;
    long value;

    if(text==NULL || out_id ==NULL || *text == '\0'){
        return ERR_INVALID_PARAMETERS;
    }

    value = strtol(text, &endptr,10);
    if(*endptr!='\0' || value <0){
        return ERR_INVALID_PARAMETERS;
    }

    *out_id = (device_id )value;
    return OK;
}

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
    return ERR_DEVICE_TYPE_MISMATCH;
}

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

static int validate_parent_for_link(const controller *ctrl, device_id parent_id){
    const device *parent_entry;

    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }

    if(parent_id == CONTROLLER_ID){
        return OK;
    }

    parent_entry = controller_find_device_const(ctrl, parent_id);
    if(parent_entry == NULL){
        return ERR_DEVICE_NOT_FOUND;
    }

    if(!device_is_control(parent_entry->info.type)){
        return ERR_DEVICE_TYPE_MISMATCH;
    }

    return OK;
}

static int validate_link_request(const controller *ctrl, device_id child_id, device_id parent_id){
    int rc;
    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(child_id == parent_id){
        return ERR_SELF_LINK;
    }
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

int cmd_list(controller *ctrl, const parsed_command *cmd){
    (void)cmd;
    if(ctrl == NULL){
        return ERR_INVALID_PARAMETERS;
    }
    return controller_list_devices(ctrl);
}

int cmd_add(controller *ctrl, const parsed_command *cmd){
    device_type type;
    int rc;

    if(ctrl==NULL || cmd==NULL){
        return ERR_INVALID_PARAMETERS;
    }
    if(cmd->argc !=1){
        return ERR_INVALID_PARAMETERS;
    }

    rc = parse_device_type_arg(cmd->argv[0], &type);
    if(rc!= OK){
        return rc;
    }

    if(type ==DEVICE_CONTROLLER || type == DEVICE_UNKNOWN){
        return ERR_DEVICE_TYPE_MISMATCH;
    }
    return controller_add_device(ctrl, type);
}


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
    if(id==CONTROLLER_ID){
        return ERR_NOT_ALLOWED;
    }

    return controller_delete_device(ctrl, id);
}

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

    if(cmd->argv[1][0]=='\0'|| cmd->argv[2][0]=='\0'){
        return ERR_INVALID_PARAMETERS;
    }

    return controller_switch_device(ctrl, id, cmd->argv[1], cmd->argv[2]);
}

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
    rc=validate_link_request(ctrl, child_id,parent_id);
    if(rc!=OK){
        return rc;
    }
    rc=controller_link_devices(ctrl, child_id, parent_id);
    if(rc!=OK){
        return ERR_LINK_FAILED;
    }
    return OK;
}

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
            ctrl->running=0;
            return OK;
        case PARSER_CMD_INVALID:
        default:
            return ERR_INVALID_COMMAND;
    }
}