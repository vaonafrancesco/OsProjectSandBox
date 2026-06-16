#ifndef SERIALIZATION_H
#define SERIALIZATION_H

#include <stddef.h>
#include "protocol.h"

int serialize_message(const domo_message *msg, char *buffer, size_t max_len);
int deserialize_message(char *buffer, domo_message *msg);

#endif