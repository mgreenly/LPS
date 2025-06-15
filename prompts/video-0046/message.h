#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Defines the types of messages that can be exchanged.
typedef enum {
    MSG_ECHO = 0,
    MSG_REVERSE = 1,
    MSG_TIME = 2
} MessageType;

// The fixed-length message header.
// The `packed` attribute is crucial to prevent compiler padding, ensuring
// the struct has the same memory layout on all platforms.
typedef struct __attribute__((packed)) {
    uint32_t type;   // The message type, from the MessageType enum.
    uint32_t length; // The length of the message body that follows.
} MessageHeader;

#endif // MESSAGE_H