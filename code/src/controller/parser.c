#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "error_codes.h"
#include "parser.h"

static void trim_newline(char *s) {
    size_t n;
    if (s == NULL) return;
    n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') {
        s[n - 1] = '\0';
    }
}

int parse_command_line(const char *line, parsed_command *out) {
    char buf[LINE_MAX];
    char *tok;
    char *saveptr = NULL;
    int argc = 0;

    if (line == NULL || out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }

    memset(out, 0, sizeof(*out));
    snprintf(buf, sizeof(buf), "%s", line);
    trim_newline(buf);

    tok = strtok_r(buf, " \t", &saveptr);
    if (tok == NULL) {
        out->type = PARSER_CMD_INVALID;
        return OK;
    }

    if (strcmp(tok, "list") == 0) {
        out->type = PARSER_CMD_LIST;
        return OK;
    }

    if (strcmp(tok, "help") == 0) {
        out->type = PARSER_CMD_HELP;
        return OK;
    }

    if (strcmp(tok, "exit") == 0 || strcmp(tok, "quit") == 0) {
        out->type = PARSER_CMD_EXIT;
        return OK;
    }

    if (strcmp(tok, "add") == 0) {
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_ADD;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        return OK;
    }

    if (strcmp(tok, "del") == 0) {
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_DEL;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        return OK;
    }

    if (strcmp(tok, "info") == 0) {
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_INFO;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        return OK;
    }

    if (strcmp(tok, "switch") == 0) {
        out->type = PARSER_CMD_SWITCH;

        while ((tok = strtok_r(NULL, " \t", &saveptr)) != NULL && argc < 3) {
            snprintf(out->argv[argc], sizeof(out->argv[argc]), "%s", tok);
            argc++;
        }

        if (argc != 3) {
            return ERR_INVALID_PARAMETERS;
        }

        out->argc = argc;
        return OK;
    }

    if (strcmp(tok, "link") == 0) {
        char *id1;
        char *to_kw;
        char *id2;

        id1 = strtok_r(NULL, " \t", &saveptr);
        to_kw = strtok_r(NULL, " \t", &saveptr);
        id2 = strtok_r(NULL, " \t", &saveptr);

        if (id1 == NULL || to_kw == NULL || id2 == NULL) {
            return ERR_INVALID_PARAMETERS;
        }

        if (strcmp(to_kw, "to") != 0) {
            return ERR_INVALID_PARAMETERS;
        }

        out->type = PARSER_CMD_LINK;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", id1);
        snprintf(out->argv[1], sizeof(out->argv[1]), "%s", id2);
        out->argc = 2;
        return OK;
    }

    out->type = PARSER_CMD_INVALID;
    return ERR_INVALID_COMMAND;
}