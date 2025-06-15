// message.h

#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Defines the types of messages that can be sent.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} MessageType;

// Defines the fixed-length message header.
// The 'packed' attribute is crucial to prevent compiler-inserted padding,
// ensuring the struct has the same memory layout on client and server.
typedef struct __attribute__((packed)) {
    uint32_t type;      // MessageType enum
    uint32_t length;    // Length of the body that follows
} MessageHeader;

#endif // MESSAGE_H