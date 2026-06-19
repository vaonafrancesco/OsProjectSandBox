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

int cleanup_install_sigchld_handler(void) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction(SIGCHLD)");
        return -1;
    }

    return 0;
}

int cleanup_has_pending_sigchld(void) {
    return g_child_died ? 1 : 0;
}

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

    while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (controller_finalize_dead_device(ctrl, dead_pid, status) != OK) {
            // Debug log - commented out
            /**
             * Quando: Quando un processo figlio muore ma il controller fallisce nel finalizzarlo (rimuoverlo dal sistema)
Cosa stampa: Il PID del processo che non è stato finalizzato correttamente
             */
            fprintf(stderr,
                 "\n[cleanup] Failed to finalize pid=%ld\n",
                    (long)dead_pid);
        }
        reaped_count++;
    }

    if (dead_pid == -1 && errno != ECHILD) {
        perror("waitpid");
        return -1;
    }

    return reaped_count;
}