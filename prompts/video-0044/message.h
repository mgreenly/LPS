#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Define the types of messages that can be exchanged.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} message_type_t;

// Define the fixed-size message header.
// The 'packed' attribute is crucial to prevent compiler-inserted padding,
// ensuring a consistent binary layout across different architectures.
typedef struct {
    message_type_t type;
    uint32_t length; // Length of the message body (the string)
} __attribute__((packed)) message_header_t;

#endif // MESSAGE_H