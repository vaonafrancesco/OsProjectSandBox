#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cleanup.h"
#include "controller.h"
#include "device.h"
#include "routing.h"
#include "error_codes.h"

//#define DOMOTICA_REGISTRY_PATH "/tmp/domotica_registry"

static volatile sig_atomic_t g_child_died = 0;

int write_registry(const controller *controller);

// Minimal signal handler: just sets the flag to be processed asynchronously by the main loop.
static void sigchld_handler(int signo) {
    (void)signo;
    g_child_died = 1;
}


/*static int cleanup_write_registry(const controller *ctrl) {
    FILE *fp = fopen(DOMOTICA_REGISTRY_PATH, "w");
    if (fp == NULL) {
        perror("fopen(/tmp/domotica_registry)");
        return -1;
    }

    fprintf(fp, "# ID   TYPE\n");
    fprintf(fp, "0      controller\n");

    for (int i = 0; i < ctrl->device_count; ++i) {
        const device *dev = &ctrl->devices[i];
        fprintf(fp, "%d      %s\n", dev->info.id, device_type_str(dev->info.type));
    }

    if (fclose(fp) != 0) {
        perror("fclose(/tmp/domotica_registry)");
        return -1;
    }

    return 0;
}*/

// Registers the SIGCHLD handler to detect when child processes terminate.
int cleanup_install_sigchld_handler(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    
    // SA_RESTART: Prevents interrupted system calls (like select/read) from failing.
    // SA_NOCLDSTOP: Only triggers on actual termination, ignoring paused children.
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction(SIGCHLD)");
        return -1;
    }

    return 0;
}

// Checks if the signal handler has caught a termination event.
int cleanup_has_pending_sigchld(void) {
    return g_child_died ? 1 : 0;
}

// TERMINATED PROCESS HANDLER
// Cleans up terminated children using waitpid to prevent them from becoming zombie processes.
int cleanup_reap_terminated_children(controller *ctrl)
{
    int status = 0;
    pid_t dead_pid;
    int reaped_count = 0;

    if (ctrl == NULL) {
        errno = EINVAL;
        return -1;
    }

    g_child_died = 0;
    
    // WNOHANG ensures waitpid doesn't block the controller if no children have terminated.
    // A loop is used to catch multiple children terminating simultaneously.
    while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0) {
    	// Remove the device from the logical system and routing tables
        if (controller_finalize_dead_device(ctrl, dead_pid, status) != OK) {
            
            fprintf(stderr,
                    "\n[cleanup] Failed to finalize pid=%ld\n",
                    (long)dead_pid);
        }
        reaped_count++;
    }

	// ECHILD simply means there are no children left to wait for, which is a normal state.
    if (dead_pid == -1 && errno != ECHILD) {
        perror("waitpid");
        return -1;
    }

    return reaped_count;
}
