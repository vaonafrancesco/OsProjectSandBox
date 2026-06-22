#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "controller.h"
#include "device.h"
#include "error_codes.h"
#include "parser.h"
#include "repl.h"

/*	Compare the string typed by the user to find the right device type.
	If the string doesn't match any known device, return DEVICE_UNKNOWN.*/
static device_type parse_device_type(const char *s) {
    if (s == NULL) return DEVICE_UNKNOWN;
    if (strcmp(s, "bulb") == 0) return DEVICE_BULB;
    if (strcmp(s, "hub") == 0) return DEVICE_HUB;
    if (strcmp(s, "timer") == 0) return DEVICE_TIMER;
    if (strcmp(s, "window") == 0) return DEVICE_WINDOW;
    if (strcmp(s, "fridge") == 0) return DEVICE_FRIDGE;
    return DEVICE_UNKNOWN;
}

// Safely convert a string into a numeric device ID.
static device_id parse_id(const char *s) {
    char *endptr;
    long value;

    if (s == NULL || *s == '\0') {
        return -1;	// Empty string is not a valid ID
    }

    errno = 0;
    // strtol helps us catch invalid characters mixed with numbers
    value = strtol(s, &endptr, 10);
	
	// If there was an overflow, or if the string contained letters, it's an error
    if (errno != 0 || endptr == s || *endptr != '\0') {
        return -1;
    }
	
	// IDs can't be negative or larger than the max int
    if (value < 0 || value > INT_MAX) {
        return -1;
    }

    return (device_id)value;
}

/*	Print the shell prompt so the user knows the program is ready for input.
	We use fflush to make sure it prints immediately without waiting for a newline.*/
int repl_print_prompt(void) {
    printf("domotics> ");
    fflush(stdout);
    return OK;
}

/*	This is the core of our interactive shell.
	It reads a single line from the terminal, figures out what command it is, and runs it. */
int repl_read_and_execute(controller *controller) {
    char line[LINE_MAX];
    parsed_command cmd;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
	
	/* 	Read user input. If it fails (like if the user presses Ctrl+D), 
    	we set running to 0 so the program can shut down gracefully. */
    if (fgets(line, sizeof(line), stdin) == NULL) {
        controller->running = 0;
        return OK;
    }
	
	// Translate the raw string into a structured command
    rc = parse_command_line(line, &cmd);
    if (rc != OK) {
        fprintf(stderr, "Parse error.\n");
        return rc;
    }

	// Execute the right action based on what the parser found
    switch (cmd.type) {
        case PARSER_CMD_LIST:
            rc = controller_list_devices(controller);
            break;

        case PARSER_CMD_ADD: {
        	// We need to convert the text argument into an actual enum type first
            device_type type = parse_device_type(cmd.argv[0]);
            if (type == DEVICE_UNKNOWN) {
                rc = ERR_INVALID_PARAMETERS;
            } else {
                rc = controller_add_device(controller, type);
            }
            break;
        }

        case PARSER_CMD_DEL:
            rc = controller_delete_device(controller, parse_id(cmd.argv[0]));
            break;

        case PARSER_CMD_INFO:
            rc = controller_info_device(controller, parse_id(cmd.argv[0]));
            break;

        case PARSER_CMD_SWITCH:
            rc = controller_switch_device(controller,
                                          parse_id(cmd.argv[0]),
                                          cmd.argv[1],
                                          cmd.argv[2]);
            break;

        case PARSER_CMD_LINK:
            rc = controller_link_devices(controller,
                                         parse_id(cmd.argv[0]),
                                         parse_id(cmd.argv[1]));
            break;

        case PARSER_CMD_HELP:
        	 // Just print a reminder of how to use the commands
            printf("Commands:\n");
            printf("  list\n");
            printf("  add <device>\n");
            printf("  del <id>\n");
            printf("  link <id1> to <id2>\n");
            printf("  switch <id> <label> <pos>\n");
            printf("  info <id>\n");
            printf("  exit\n");
            rc = OK;
            break;

        case PARSER_CMD_EXIT:
        	// Break the main loop to exit the program
            controller->running = 0;
            rc = OK;
            break;

        default:
            fprintf(stderr, "Command not valid.\n");
            rc = ERR_INVALID_COMMAND;
            break;
    }
	
	// If any of the commands failed, let the user know what went wrong
    if (rc != OK) {
        fprintf(stderr, "Error: %s\n", error_str(rc));
    }

    return rc;
}
