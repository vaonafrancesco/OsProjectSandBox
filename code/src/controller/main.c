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
    
     // CHILD PROCESS MODE (DEVICE)
    /*	When the controller uses fork() and execl(), it passes special arguments
    	like "--device-bulb 3". This block catches those arguments.
   		If a match is found, this program stops being the controller and 
    	runs the specific device logic instead. */

    if (argc == 3 && strcmp(argv[1], "--device-bulb") == 0) {
        device_id id= (device_id)atoi(argv[2]);	// Convert the ID from string to int
        return bulb_device_main(id);			// Enter the bulb infinite loop
    }
	if (argc == 3 && strcmp(argv[1], "--device-window") == 0) {
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
    
    // MAIN PROCESS MODE (CONTROLLER)
    /*	If we reach this point, it means no special arguments were passed.
    	This happens when the user manually runs the program from the terminal.
    	So, we act as the main controller. */
    	
    // 1. Initialize the controller (setup variables, create runtime folders)
    rc = controller_init(&controller);
    if (rc != OK) {
        fprintf(stderr, "controller_init failed: %s\n", error_str(rc));
        return rc;
    }
	
	// 2. Setup the SIGCHLD signal handler.
	/*	This tells the OS to notify us whenever a child process dies or crashes,
    	so we can clean it up and avoid "zombie" processes. */
    rc = cleanup_install_sigchld_handler();
    if (rc != OK) {
        fprintf(stderr, "cleanup_install_sigchld_handler failed: %s\n", error_str(ERR_SYSTEM));
        controller_destroy(&controller);
        return ERR_SYSTEM;
    }
	
	// 3. Start the main loop 
    rc = controller_run(&controller);
    if (rc != OK) {
        fprintf(stderr, "controller_run failed: %s\n", error_str(rc));
    }
	
	// 4. Shut down sequence.
	/*	When the user types "exit", controller_run finishes. 
    	We then destroy the controller, which kills all remaining child processes safely. */
    {
        int cleanup_rc = controller_destroy(&controller);
        if (cleanup_rc != OK && rc == OK) {
            rc = cleanup_rc;
        }
    }

    return rc;
}
