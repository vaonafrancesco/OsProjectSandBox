#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "controller.h"
#include "error_codes.h"

int bulb_device_main(device_id_t id);

int main(int argc, char **argv) {
    controller_t controller;
    int rc;

    if (argc == 3 && strcmp(argv[1], "--device-bulb") == 0) {
        device_id_t id = (device_id_t)atoi(argv[2]);
        return bulb_device_main(id);
    }

    rc = controller_init(&controller);
    if (rc != DOMO_OK) {
        fprintf(stderr, "controller_init failed: %s\n", domo_error_str(rc));
        return rc;
    }

    rc = controller_run(&controller);
    if (rc != DOMO_OK) {
        fprintf(stderr, "controller_run failed: %s\n", domo_error_str(rc));
    }

    {
        int cleanup_rc = controller_destroy(&controller);
        if (cleanup_rc != DOMO_OK && rc == DOMO_OK) {
            rc = cleanup_rc;
        }
    }

    return rc;
}