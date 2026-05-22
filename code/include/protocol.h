#ifndef PROTOCOL_H
#define PROTOCOL_H


//ERROR CODES: Must match EXACTLY with the numeric values used in Bash.
// naming convention: UPPER_SNAKE_CASE

#define OK                      0
#define DEVICE_NOT_FOUND        1
#define INVALID_COMMAND         2
#define LINK_FAILED             3
#define DEVICE_TYPE_MISMATCH    4
#define IPC_ERROR               5
#define CYCLE_DETECTED          6

//SYSTEM PATHS AND PREFIXES

#define REGISTRY_PATH       "/tmp/domotica_registry"
#define FIFO_PATH_PREFIX    "/tmp/domotica_fifo_"

//MESSAGE PROTOCOL AND LIMITS: format: SENDER_ID|COMMAND|TARGET_ID|PAYLOAD

#define MAX_MSG_LEN         1024    //maximum buffer size for a message
#define MSG_DELIMITER       "|"     // delimiter used for strtok
#define MSG_DELIMITER_CHAR  '|'     // delimiter as a single character
#define NEWLINE_CHAR        '\n'    // mandatory message terminator

// SPECIAL IDENTIFIERS

#define CONTROLLER_ID       0       //ID 0 is strictly reserved for the Controller
#define EXT_SENDER_ID       "EXT"   //ID used by manual_interaction.sh

//VALID COMMANDS: exact string command expected by the IPC contract.

#define CMD_SWITCH          "SWITCH"
#define CMD_LINK            "LINK"
#define CMD_INFO            "INFO"
#define CMD_DEL             "DEL"
#define CMD_STATUS          "STATUS"

// TIMEOUT CONFIGURATION: Must be > 3 to account or the mandatory 1-3s delay of devices
#define TIMEOUT_DEVICE 7


#endif