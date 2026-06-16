#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "error_codes.h"
#include "cleanup.h"

int bulb_device_main(device_id id);
int window_device_main(device_id id);
int fridge_device_main(device_id id);
int hub_device_main(device_id id);
int timer_device_main(device_id id);

int main(int argc, char **argv) {
    controller controller;
    int rc;

    if (argc == 3 && strcmp(argv[1], "--device-bulb") == 0) {
        device_id id= (device_id)atoi(argv[2]);
        return bulb_device_main(id);
    }if (argc == 3 && strcmp(argv[1], "--device-window") == 0) {
        device_id id= (device_id)atoi(argv[2]);
        return window_device_main(id);
    }

    if (argc == 3 &&strcmp(argv[1],"--device-fridge") ==0) {
        device_id id= (device_id)atoi(argv[2]);
        return fridge_device_main(id) ;
    }
    if (argc == 3 && strcmp(argv[1], "--device-hub") == 0) {
        device_id id= (device_id)atoi(argv[2]);
        return hub_device_main(id);
    }if (argc == 3 && strcmp(argv[1], "--device-timer") == 0) {
        device_id id= (device_id)atoi(argv[2]);
        return timer_device_main(id);
    }

    rc = controller_init(&controller);
    if (rc != OK) {
        fprintf(stderr, "controller_init failed: %s\n", error_str(rc));
        return rc;
    }

    rc = cleanup_install_sigchld_handler();
    if (rc != OK) {
        fprintf(stderr, "cleanup_install_sigchld_handler failed: %s\n", error_str(ERR_SYSTEM));
        controller_destroy(&controller);
        return ERR_SYSTEM;
    }

    rc = controller_run(&controller);
    if (rc != OK) {
        fprintf(stderr, "controller_run failed: %s\n", error_str(rc));
    }

    {
        int cleanup_rc = controller_destroy(&controller);
        if (cleanup_rc != OK && rc == OK) {
            rc = cleanup_rc;
        }
    }

    return rc;
}