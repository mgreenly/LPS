/**
 * @file hello-world.c
 * @brief A production-ready "Hello, World!" program.
 *
 * This program demonstrates best practices for writing portable and robust C code
 * targeting multiple architectures (x86_64, AARCH64). It adheres to the C17
 * standard and uses fixed-width integer types for data-size guarantees.
 */

#include <stdio.h>    // For printf
#include <stdlib.h>   // For EXIT_SUCCESS
#include <stdint.h>   // For fixed-width integer types like uint64_t
#include <inttypes.h> // For portable format specifiers like PRIu64

/**
 * @brief Main entry point of the program.
 *
 * Prints a greeting message to standard output and demonstrates the use of
 * fixed-width integers and their corresponding portable format specifiers.
 *
 * @return Returns EXIT_SUCCESS to indicate successful execution.
 */
int main(void) {
    // Use a fixed-width integer to guarantee its size across platforms.
    // This is crucial for data structures, file formats, or network protocols.
    // uint64_t is guaranteed to be an unsigned 64-bit integer on both
    // x86_64 and AARCH64.
    const uint64_t iteration_count = 1;

    // Print the message.
    // Use the PRIu64 macro from <inttypes.h> for printing a uint64_t.
    // This ensures the correct format specifier is used regardless of the
    // underlying type, preventing undefined behavior and compiler warnings.
    // The C preprocessor concatenates the string literals into a single
    // format string.
    printf("Hello, World! Iteration: %" PRIu64 "\n", iteration_count);

    // Return a portable success code.
    return EXIT_SUCCESS;
}