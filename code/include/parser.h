#ifndef PARSER_H
#define PARSER_H

#include "common.h"

typedef enum {
    PARSER_CMD_INVALID = 0,
    PARSER_CMD_LIST,
    PARSER_CMD_ADD,
    PARSER_CMD_DEL,
    PARSER_CMD_LINK,
    PARSER_CMD_SWITCH,
    PARSER_CMD_INFO,
    PARSER_CMD_EXIT,
    PARSER_CMD_HELP
} parser_cmd_type ;

typedef struct {
    parser_cmd_type type;
    char argv[5][VALUE_MAX];
    int argc;
} parsed_command ;

int parse_command_line(const char *line, parsed_command *out);

#endif