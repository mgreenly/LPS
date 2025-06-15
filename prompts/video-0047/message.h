#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Defines the types of messages that can be exchanged.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} MessageType;

// The fixed-size header for every message.
// The total message size on the wire is sizeof(MessageHeader) + message_len.
typedef struct {
    uint32_t type;        // The type of the message, cast from MessageType
    uint32_t message_len; // The length of the body, including the null terminator
} MessageHeader;

#endif // MESSAGE_H