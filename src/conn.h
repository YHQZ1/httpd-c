#ifndef CONN_H
#define CONN_H

#include <netinet/in.h>
#include <stddef.h>
#include <time.h>

#include "httpd.h"

enum
{
  CONN_KEEP = 0,
  CONN_CLOSE = 1
};

// One client connection. Requests are parsed out of rbuf; at most one
// response (wbuf) is in flight at a time, so pipelined requests are handled
// in order and a slow reader applies backpressure.
typedef struct conn
{
  int fd;
  char ip[INET_ADDRSTRLEN];
  unsigned port;

  char rbuf[HTTP_MAX_HEADER_BYTES];
  size_t rlen;

  char *wbuf; // non-NULL while a response is being written
  size_t wlen;
  size_t woff;
  int close_after;
  int lingering; // final response sent and write side shut down; discarding input

  int status; // status of the response in flight
  char log_req[128];
  struct timespec req_start;
  int req_started;

  time_t last_active;
  int want_write; // interest currently registered with the event loop
} conn;

conn *conn_new(int fd, const struct sockaddr_in *peer);
void conn_free(conn *c); // closes the fd

// Each returns CONN_KEEP or CONN_CLOSE.
int conn_on_readable(conn *c);
int conn_on_writable(conn *c);

int conn_wants_write(const conn *c);
int conn_is_idle(const conn *c); // no partial request, nothing to write

#endif // CONN_H
