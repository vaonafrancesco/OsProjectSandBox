#ifndef __REPL_H__
#define __REPL_H__

#include "controller.h"

int repl_print_prompt(void);
int repl_handle_line(controller *ctrl, const char *line);
int repl_read_and_execute(controller *ctrl);




#endif
