#include <stdio.h>
#include <string.h>

#include "controller.h"
#include "device.h"
#include "error_codes.h"
#include "parser.h"

static device_type parse_device_type(const char *s) {
    if (s == NULL) return -1;
    if (strcmp(s, "bulb") == 0) return DEVICE_BULB;
    if (strcmp(s, "hub") == 0) return DEVICE_HUB;
    if (strcmp(s, "timer") == 0) return DEVICE_TIMER;
    if (strcmp(s, "window") == 0) return DEVICE_WINDOW;
    if (strcmp(s, "fridge") == 0) return DEVICE_FRIDGE;
    return -1;
}

static device_id parse_id(const char *s) {
    int id = -1;
    if (s != NULL) {
        sscanf(s, "%d", &id);
    }
    return id;
}

int controller_run(controller *controller) {
    char line[LINE_MAX];
    parsed_command cmd;
    int rc;

    if (controller == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    printf("Domotics controller started.\n");
    printf("Type 'help' for available commands.\n");

    while (controller->running) {
        printf("domotics> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        rc = parse_command_line(line, &cmd);
        if (rc != OK) {
            fprintf(stderr, "Parse error.\n");
            continue;
        }

        switch (cmd.type) {
            case PARSER_CMD_LIST:
                rc = controller_list_devices(controller);
                break;

            case PARSER_CMD_ADD: {
                device_type type = parse_device_type(cmd.argv[0]);
                if (type < 0) {
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
                controller->running = 0;
                rc = OK;
                break;

            default:
                fprintf(stderr, "Command not valid.\n");
                rc = ERR_INVALID_COMMAND;
                break;
        }

        if (rc != OK) {
            fprintf(stderr, "Error: %s\n", error_str(rc));
        }
    }

    return OK;
}