#ifndef SIGNAL_H
#define SIGNAL_H

// A global flag to signal shutdown. It's volatile to prevent compiler
// optimization and sig_atomic_t to ensure atomic access in the signal handler.
extern volatile sig_atomic_t g_shutdown_flag;

// Sets up signal handlers for SIGINT and SIGTERM.
void setup_signal_handlers(void);

#endif // SIGNAL_H