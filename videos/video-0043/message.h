#ifndef MESSAGE_H
#define MESSAGE_H

#include <stdint.h>

// Define message types using an enum for clarity and type safety.
typedef enum {
    MSG_ECHO,
    MSG_REVERSE,
    MSG_TIME
} MessageType;

// Define the fixed-size message header.
// Using fixed-width integers from stdint.h is critical for cross-platform
// compatibility (e.g., between x86_64 and AARCH64).
// The __attribute__((packed)) is important to prevent the compiler from
// adding padding between fields, ensuring sizeof(MessageHeader) is exact.
typedef struct __attribute__((packed)) {
    uint32_t type;          // MessageType enum
    uint32_t body_length;   // Length of the message body that follows
} MessageHeader;

#endif // MESSAGE_H