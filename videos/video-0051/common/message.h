#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Define the different types of messages the server can handle.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} message_type_t;

// The fixed-size header for every message.
// #pragma pack is used to ensure the struct is packed without padding.
// This is critical for consistent binary representation across the network.
#pragma pack(push, 1)
typedef struct {
    uint32_t type;   // The type of the message (from message_type_t)
    uint32_t length; // The length of the message body that follows
} message_header_t;
#pragma pack(pop)

// In-memory representation of a message, used by server and client logic.
typedef struct {
    int client_fd; // Socket descriptor of the client (server-side only)
    message_header_t header;
    char* body;    // Dynamically allocated message body
} message_t;

#endif // MESSAGE_H