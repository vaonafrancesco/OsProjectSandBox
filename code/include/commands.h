#ifndef __COMMANDS_H__
#define __COMMANDS_H__

#include "controller.h"
#include "parser.h"

int cmd_list(controller *ctrl, const parsed_command *cmd);
int cmd_add(controller *ctrl, const parsed_command *cmd);
int cmd_del(controller *ctrl, const parsed_command *cmd);
int cmd_info(controller *ctrl, const parsed_command *cmd);
int cmd_switch(controller *ctrl, const parsed_command *cmd);
int cmd_link(controller *ctrl, const parsed_command *cmd);
int cmd_help(void);

int execute_parsed_command(controller *ctrl, const parsed_command *cmd);






#endif