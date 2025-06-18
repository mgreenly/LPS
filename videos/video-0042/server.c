// server.c - A simple "Hello World" server demonstration.
// Intended for production use, targeting x86_64 and AARCH64 Linux.

#include <stdio.h>      // For printf, perror
#include <stdlib.h>     // For EXIT_SUCCESS, EXIT_FAILURE
#include <stdint.h>     // For fixed-width integer types (per requirement)
#include <inttypes.h>   // For fixed-width integer formatting (per requirement)

/**
 * @brief Main entry point for the server application.
 *
 * This program prints a startup message to standard output and exits.
 * It includes robust error checking for the print operation and uses
 * standard exit codes.
 *
 * @return Returns EXIT_SUCCESS on success, EXIT_FAILURE on error.
 */
int main(void) {
    // Although this program doesn't use fixed-width integers, the headers
    // are included to adhere to the specified coding standards.
    // An example of their usage would be:
    // uint64_t request_id = 12345;
    // printf("Processing request %" PRIu64 "\n", request_id);

    if (printf("Hello from the server\n") < 0) {
        // Handle potential printf errors, e.g., if stdout is a broken pipe.
        perror("printf failed");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}