#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "conn.h"
#include "http_parser.h"
#include "metrics.h"
#include "response.h"
#include "router.h"
#include "server.h"

enum
{
  FL_DONE,
  FL_AGAIN,
  FL_ERR
};

conn *conn_new(int fd, const struct sockaddr_in *peer)
{
  conn *c = calloc(1, sizeof(*c));
  if (!c)
  {
    return NULL;
  }
  c->fd = fd;
  inet_ntop(AF_INET, &peer->sin_addr, c->ip, sizeof(c->ip));
  c->port = ntohs(peer->sin_port);
  c->last_active = time(NULL);
  metrics_conn_opened();
  return c;
}

void conn_free(conn *c)
{
  if (!c)
  {
    return;
  }
  close(c->fd);
  free(c->wbuf);
  metrics_conn_closed();
  free(c);
}

int conn_wants_write(const conn *c) { return c->wbuf != NULL; }

int conn_is_idle(const conn *c) { return c->wbuf == NULL && c->rlen == 0; }

static double elapsed_since(const struct timespec *start)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)(now.tv_sec - start->tv_sec) +
         (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

// Copies the request line for the access log, scrubbing control bytes so a
// client can't forge log lines.
static void capture_request_line(conn *c)
{
  size_t i = 0;
  while (i < sizeof(c->log_req) - 1 && i < c->rlen && c->rbuf[i] != '\r' &&
         c->rbuf[i] != '\n')
  {
    unsigned char ch = (unsigned char)c->rbuf[i];
    c->log_req[i] = (ch < 0x20 || ch >= 0x7f) ? '?' : (char)ch;
    i++;
  }
  c->log_req[i] = '\0';
}

static void queue_response(conn *c, http_response *resp, int keep_alive,
                           int head_only)
{
  c->status = resp->status;
  c->close_after = !keep_alive;
  c->woff = 0;
  c->wbuf = response_serialize(resp, keep_alive, head_only, &c->wlen);
  if (!c->wbuf)
  {
    c->close_after = 1; // out of memory: give up on this connection
    c->status = 500;
  }
  response_free(resp);
}

static int flush(conn *c)
{
  while (c->woff < c->wlen)
  {
    ssize_t w = write(c->fd, c->wbuf + c->woff, c->wlen - c->woff);
    if (w < 0)
    {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN || errno == EWOULDBLOCK)
        return FL_AGAIN;
      return FL_ERR;
    }
    c->woff += (size_t)w;
    c->last_active = time(NULL);
  }
  return FL_DONE;
}

// Called once the response has been fully written.
static void finish_request(conn *c)
{
  double secs = c->req_started ? elapsed_since(&c->req_start) : 0;
  metrics_observe_request(c->status, secs);
  printf("%s:%u \"%s\" %d %zu %.2fms\n", c->ip, c->port, c->log_req, c->status,
         c->wlen, secs * 1000);

  free(c->wbuf);
  c->wbuf = NULL;
  c->wlen = c->woff = 0;
  c->req_started = 0;
}

// After our last response on a connection we don't close() straight away: if
// the client still has unread bytes queued (an oversized request, say), closing
// makes the kernel send RST, which can destroy the response before the client
// reads it. Send FIN instead, swallow whatever else arrives, and close once the
// client does (or after LINGER_TIMEOUT_SEC).
static int begin_linger(conn *c)
{
  if (shutdown(c->fd, SHUT_WR) < 0)
  {
    return CONN_CLOSE;
  }
  c->lingering = 1;
  c->rlen = 0;
  c->last_active = time(NULL);
  return CONN_KEEP;
}

static int status_for_parse_error(http_parse_status st)
{
  switch (st)
  {
  case HTTP_PARSE_BAD_VERSION:  return 505;
  case HTTP_PARSE_URI_TOO_LONG: return 414;
  case HTTP_PARSE_TOO_LARGE:    return 431;
  default:                      return 400;
  }
}

// Handles every complete request currently buffered, stopping when a response
// can't be written in full yet or the buffer holds only a partial request.
static int process(conn *c)
{
  while (c->wbuf == NULL && c->rlen > 0)
  {
    if (!c->req_started)
    {
      clock_gettime(CLOCK_MONOTONIC, &c->req_start);
      c->req_started = 1;
    }

    http_request req;
    size_t used = 0;
    http_parse_status st = http_parse_request(c->rbuf, c->rlen, &req, &used);
    if (st == HTTP_PARSE_INCOMPLETE)
    {
      return CONN_KEEP;
    }
    capture_request_line(c);

    http_response resp;
    response_init(&resp);
    int keep_alive;
    int head_only = 0;
    if (st == HTTP_PARSE_OK)
    {
      router_handle(&req, &resp);
      head_only = sv_eq(req.method, "HEAD");
      // We never read request bodies, so a request that has one can't be
      // followed by another on this connection.
      keep_alive = req.keep_alive && !req.has_body && !server_draining();
    }
    else
    {
      response_error(&resp, status_for_parse_error(st));
      keep_alive = 0;
    }
    queue_response(c, &resp, keep_alive, head_only);

    if (st == HTTP_PARSE_OK)
    {
      memmove(c->rbuf, c->rbuf + used, c->rlen - used);
      c->rlen -= used;
    }
    else
    {
      c->rlen = 0;
    }

    if (!c->wbuf)
    {
      return CONN_CLOSE;
    }
    int f = flush(c);
    if (f == FL_ERR)
    {
      return CONN_CLOSE;
    }
    if (f == FL_AGAIN)
    {
      return CONN_KEEP; // resumes in conn_on_writable
    }
    int close_it = c->close_after;
    finish_request(c);
    if (close_it)
    {
      return begin_linger(c);
    }
  }
  return CONN_KEEP;
}

int conn_on_readable(conn *c)
{
  if (c->lingering)
  {
    char sink[4096];
    ssize_t n = read(c->fd, sink, sizeof(sink));
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    {
      return CONN_KEEP;
    }
    return n > 0 ? CONN_KEEP : CONN_CLOSE; // EOF or error: client is done
  }
  if (c->wbuf || c->rlen >= sizeof(c->rbuf))
  {
    return c->wbuf ? CONN_KEEP : CONN_CLOSE;
  }

  ssize_t n = read(c->fd, c->rbuf + c->rlen, sizeof(c->rbuf) - c->rlen);
  if (n == 0)
  {
    return CONN_CLOSE; // peer closed
  }
  if (n < 0)
  {
    return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
               ? CONN_KEEP
               : CONN_CLOSE;
  }
  c->rlen += (size_t)n;
  c->last_active = time(NULL);
  return process(c);
}

int conn_on_writable(conn *c)
{
  if (!c->wbuf)
  {
    return CONN_KEEP;
  }
  int f = flush(c);
  if (f == FL_ERR)
  {
    return CONN_CLOSE;
  }
  if (f == FL_AGAIN)
  {
    return CONN_KEEP;
  }
  int close_it = c->close_after;
  finish_request(c);
  if (close_it)
  {
    return begin_linger(c);
  }
  return process(c); // pipelined requests that arrived while we were writing
}
