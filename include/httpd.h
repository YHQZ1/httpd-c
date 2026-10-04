#ifndef HTTPD_H
#define HTTPD_H

#define DEFAULT_PORT 8080
#define DEFAULT_WWW_ROOT "www"
#define HTTPD_VERSION "1.0.0"

#define BACKLOG 128
#define MAX_FDS 4096            // highest fd we track; accepts above this are dropped
#define HTTP_MAX_HEADER_BYTES 16384
#define MAX_STATIC_FILE (8 * 1024 * 1024)

#define IDLE_TIMEOUT_SEC 15     // keep-alive / stalled-client timeout
#define LINGER_TIMEOUT_SEC 2    // how long to swallow a client's unread bytes after our final response
#define SHUTDOWN_GRACE_SEC 5    // max time to drain connections on SIGTERM

#endif // HTTPD_H
