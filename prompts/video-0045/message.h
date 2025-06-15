#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Use a pragma for simplicity, but include guards are also fine.
#pragma once

// Defines the types of messages that can be sent.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME,
} message_type_t;

// The fixed-length message header.
// The `packed` attribute is critical to prevent the compiler from adding
// padding, ensuring the struct's memory layout matches the on-wire format.
typedef struct __attribute__((packed)) {
    uint32_t type;   // The message type, corresponds to message_type_t
    uint32_t length; // The length of the message body (including null terminator)
} message_header_t;

// A sensible maximum for the message body to prevent unbounded allocation.
#define MAX_BODY_LEN 1024
#define MAX_MSG_LEN (sizeof(message_header_t) + MAX_BODY_LEN)

#endif // MESSAGE_H
