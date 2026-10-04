#ifndef EVENT_H
#define EVENT_H

// Minimal level-triggered readiness API. epoll on Linux, poll() elsewhere.
// One global loop; interest is "read" or "write" per fd.

#define EV_MAX_EVENTS 256

typedef struct
{
  int fd;
  int readable;
  int writable;
  int error; // hangup / error condition
} ev_event;

int ev_init(void);
void ev_close(void);
int ev_add(int fd, int want_read, int want_write);
int ev_mod(int fd, int want_read, int want_write);
void ev_del(int fd);

// Returns the number of events (0 on timeout or EINTR), -1 on failure.
int ev_wait(ev_event *out, int max, int timeout_ms);

const char *ev_backend(void);

#endif // EVENT_H
