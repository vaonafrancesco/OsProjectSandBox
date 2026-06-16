#ifndef CLEANUP_H
#define CLEANUP_H

#include "controller.h"

int cleanup_install_sigchld_handler(void);
int cleanup_has_pending_sigchld(void);
int cleanup_reap_terminated_children(controller *ctrl);



#endif