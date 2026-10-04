#ifndef SERVER_H
#define SERVER_H

// Creates a non-blocking listening socket on all interfaces. Returns the fd
// or -1 (errno set).
int server_listen(int port);

// Runs the event loop until server_request_shutdown() is called, then drains
// open connections (up to SHUTDOWN_GRACE_SEC) and returns.
void server_run(int listen_fd);

// Async-signal-safe.
void server_request_shutdown(void);

// True once shutdown has begun; responses then carry "Connection: close".
int server_draining(void);

#endif // SERVER_H
