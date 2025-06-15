#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

#define SERVER_PORT 4242
#define MAX_BODY_SIZE 1024

// Define the types of messages that can be sent.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} MessageType;

// The fixed-length message header.
// Using __attribute__((packed)) is crucial to prevent struct padding,
// ensuring the binary layout is identical on all platforms (x86_64, AARCH64).
typedef struct __attribute__((packed)) {
    uint32_t type;   // The type of the message, from MessageType enum
    uint32_t length; // The length of the message body (the string)
} MessageHeader;

#endif // MESSAGE_H