#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "error_codes.h"
#include "parser.h"

/*  Helper function to remove the newline character ('\n') at the end of a string.
	When you use fgets() to read user input, it usually captures the "Enter" key press too*/
static void trim_newline(char *s) {
    size_t n;
    if (s == NULL) return;
    n = strlen(s);
    if (n > 0 && s[n - 1] == '\n') {
        s[n - 1] = '\0';	// Replace newline with string terminator
    }
}

/*	Main function to read the raw text typed by the user and convert it 
	into a structured command that the controller can easily understand.*/ 
int parse_command_line(const char *line, parsed_command *out) {
    char buf[LINE_MAX];
    char *tok;
    char *saveptr = NULL;
    int argc = 0;

    if (line == NULL || out == NULL) {
        return ERR_INVALID_PARAMETERS;
    }
    
	// Clean up the output structure before we start putting data in it
    memset(out, 0, sizeof(*out));
    
    /*	We copy the input string into a local buffer because strtok_r 
    	actually modifies the string. */
    snprintf(buf, sizeof(buf), "%s", line);
    trim_newline(buf);

	// Get the first word typed by the user
    tok = strtok_r(buf, " \t", &saveptr);
    if (tok == NULL) {	// User just pressed Enter without typing anything
        out->type = PARSER_CMD_INVALID;
        return OK;
    }

	// COMMAND: list 
    if (strcmp(tok, "list") == 0) {
        out->type = PARSER_CMD_LIST;
        
        // We check if there are extra words after "list". If there are, it's a syntax error.
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// COMMAND: help 
    if (strcmp(tok, "help") == 0) {
        out->type = PARSER_CMD_HELP;
        return OK;
    }
	
	// COMMAND: exit or quit 
    if (strcmp(tok, "exit") == 0 || strcmp(tok, "quit") == 0) {
        out->type = PARSER_CMD_EXIT;
        return OK;
    }
	
	// COMMAND: add <device> 
    if (strcmp(tok, "add") == 0) {
    	// Grab the next word
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_ADD;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        
        // Check for garbage text at the end
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// COMMAND: del <id>
    if (strcmp(tok, "del") == 0) {
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_DEL;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// COMMAND: info <id>
    if (strcmp(tok, "info") == 0) {
        tok = strtok_r(NULL, " \t", &saveptr);
        if (tok == NULL) return ERR_INVALID_PARAMETERS;
        out->type = PARSER_CMD_INFO;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", tok);
        out->argc = 1;
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// COMMAND: switch <id> <label> <pos>
    if (strcmp(tok, "switch") == 0) {
        out->type = PARSER_CMD_SWITCH;
	
		// Since we need 3 arguments, we use a while loop to grab them all
        while ((tok = strtok_r(NULL, " \t", &saveptr)) != NULL && argc < 3) {
            snprintf(out->argv[argc], sizeof(out->argv[argc]), "%s", tok);
            argc++;
        }

        if (argc != 3) {	// If the user didn't provide exactly 3 words
            return ERR_INVALID_PARAMETERS;
        }

        out->argc = argc;
        
        // Ensure no extra garbage was typed after the 3 arguments
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// COMMAND: link <id1> to <id2>
    if (strcmp(tok, "link") == 0) {
        char *id1;
        char *to_kw;
        char *id2;
		
		// Grab the next three words
        id1 = strtok_r(NULL, " \t", &saveptr);
        to_kw = strtok_r(NULL, " \t", &saveptr);
        id2 = strtok_r(NULL, " \t", &saveptr);

        if (id1 == NULL || to_kw == NULL || id2 == NULL) {
            return ERR_INVALID_PARAMETERS;
        }
		
		// Specifically check if the middle word is "to"
        if (strcmp(to_kw, "to") != 0) {
            return ERR_INVALID_PARAMETERS;
        }

        out->type = PARSER_CMD_LINK;
        snprintf(out->argv[0], sizeof(out->argv[0]), "%s", id1);
        snprintf(out->argv[1], sizeof(out->argv[1]), "%s", id2);
        out->argc = 2;
        if (strtok_r(NULL, " \t", &saveptr) != NULL) {
            out->type = PARSER_CMD_INVALID;
            return ERR_INVALID_PARAMETERS;
        }
        return OK;
    }
	
	// If the very first word didn't match any of our ifs, it's an unknown command
    out->type = PARSER_CMD_INVALID;
    return ERR_INVALID_COMMAND;
}
