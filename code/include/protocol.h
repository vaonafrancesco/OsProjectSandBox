#ifndef PROTOCOL_H
#define PROTOCOL_H



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

// MESSAGE KINDS for request-reply pattern
#define MSG_REQUEST         1
#define MSG_RESPONSE        2


#endif