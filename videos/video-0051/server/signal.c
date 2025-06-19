#include <signal.h>
#include <unistd.h>
#include "signal.h"
#include "common/log.h"

volatile sig_atomic_t g_shutdown_flag = 0;

static void signal_handler(int signum) {
    // This is an async-signal-safe way to handle signals.
    // We just set a flag; the main loop will detect it and shut down gracefully.
    g_shutdown_flag = 1;
    (void)signum; // Unused
    // Writing to a file descriptor is also safe inside a signal handler.
    // This provides immediate feedback that the signal was caught.
    const char msg[] = "Signal caught, shutting down...\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
}

void setup_signal_handlers(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Restart syscalls if possible

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        log_error("Failed to register SIGINT handler");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        log_error("Failed to register SIGTERM handler");
    }
    log_info("Signal handlers for SIGINT and SIGTERM registered.");
}
