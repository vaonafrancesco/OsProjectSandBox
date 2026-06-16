#ifndef PROTOCOL_H
#define PROTOCOL_H

#define REGISTRY_PATH "/tmp/domotica_registry"
#define FIFO_PATH_PREFIX "/tmp/domotica_fifo_"

#define MAX_MSG_LEN 1024

#define MSG_DELIMITER_CHAR '|'
#define NEWLINE_CHAR '\n'

#define CONTROLLER_ID 0
#define EXT_SENDER_ID "EXT"

#define CMD_SWITCH "SWITCH"
#define CMD_LINK "LINK"
#define CMD_INFO "INFO"
#define CMD_DEL "DEL"
#define CMD_STATUS "STATUS"

#define TIMEOUT_DEVICE 7

#define MSG_REQUEST 1
#define MSG_RESPONSE 2

#endif