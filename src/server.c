#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "conn.h"
#include "event.h"
#include "httpd.h"
#include "server.h"

static conn *conns[MAX_FDS];
static int active;
static volatile sig_atomic_t shutdown_requested;
static int draining;

void server_request_shutdown(void) { shutdown_requested = 1; }

int server_draining(void) { return draining; }

static int set_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return -1;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int server_listen(int port)
{
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    return -1;
  }

  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    close(fd);
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
      listen(fd, BACKLOG) < 0 || set_nonblocking(fd) < 0)
  {
    close(fd);
    return -1;
  }
  return fd;
}

static void close_conn(int fd)
{
  ev_del(fd);
  conn_free(conns[fd]);
  conns[fd] = NULL;
  active--;
}

static void accept_ready(int listen_fd)
{
  for (;;)
  {
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);
    int fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
    if (fd < 0)
    {
      if (errno == EINTR || errno == ECONNABORTED)
      {
        continue;
      }
      if (errno == EMFILE || errno == ENFILE)
      {
        // Out of descriptors: the listener stays readable, so back off briefly
        // instead of spinning.
        perror("accept");
        struct timespec ts = {0, 10 * 1000 * 1000};
        nanosleep(&ts, NULL);
      }
      else if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        perror("accept");
      }
      return;
    }

    if (fd >= MAX_FDS || set_nonblocking(fd) < 0)
    {
      close(fd);
      continue;
    }
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    conn *c = conn_new(fd, &peer);
    if (!c)
    {
      close(fd);
      continue;
    }
    if (ev_add(fd, 1, 0) < 0)
    {
      conn_free(c);
      continue;
    }
    conns[fd] = c;
    active++;
  }
}

static void handle_event(const ev_event *e)
{
  conn *c = conns[e->fd];
  if (!c)
  {
    return; // closed earlier in this batch
  }

  int rc = CONN_KEEP;
  if (e->writable)
  {
    rc = conn_on_writable(c);
  }
  else if (e->readable)
  {
    rc = conn_on_readable(c);
  }
  else if (e->error)
  {
    rc = CONN_CLOSE;
  }

  if (rc == CONN_CLOSE)
  {
    close_conn(e->fd);
    return;
  }

  // Read while idle, write while a response is pending.
  int want_write = conn_wants_write(c);
  if (want_write != c->want_write)
  {
    if (ev_mod(e->fd, !want_write, want_write) < 0)
    {
      close_conn(e->fd);
      return;
    }
    c->want_write = want_write;
  }
}

// Closes connections that have gone quiet, or (when draining) are idle.
static void sweep(time_t now)
{
  for (int fd = 0; fd < MAX_FDS; fd++)
  {
    conn *c = conns[fd];
    if (!c)
    {
      continue;
    }
    int limit = c->lingering ? LINGER_TIMEOUT_SEC : IDLE_TIMEOUT_SEC;
    if ((draining && conn_is_idle(c)) || now - c->last_active > limit)
    {
      close_conn(fd);
    }
  }
}

void server_run(int listen_fd)
{
  if (ev_init() < 0)
  {
    perror("ev_init");
    return;
  }
  if (ev_add(listen_fd, 1, 0) < 0)
  {
    perror("ev_add");
    ev_close();
    return;
  }

  int lfd = listen_fd;
  time_t drain_deadline = 0;
  time_t last_sweep = 0;
  ev_event events[EV_MAX_EVENTS];

  for (;;)
  {
    if (shutdown_requested && !draining)
    {
      draining = 1;
      ev_del(lfd);
      close(lfd);
      lfd = -1;
      drain_deadline = time(NULL) + SHUTDOWN_GRACE_SEC;
      printf("shutting down: draining %d connection(s)\n", active);
      sweep(time(NULL));
    }
    if (draining && (active == 0 || time(NULL) >= drain_deadline))
    {
      break;
    }

    int n = ev_wait(events, EV_MAX_EVENTS, draining ? 100 : 1000);
    if (n < 0)
    {
      perror("ev_wait");
      break;
    }
    for (int i = 0; i < n; i++)
    {
      if (events[i].fd == lfd)
      {
        accept_ready(lfd);
      }
      else
      {
        handle_event(&events[i]);
      }
    }

    time_t now = time(NULL);
    if (now != last_sweep)
    {
      last_sweep = now;
      sweep(now);
    }
  }

  for (int fd = 0; fd < MAX_FDS; fd++)
  {
    if (conns[fd])
    {
      close_conn(fd);
    }
  }
  if (lfd >= 0)
  {
    close(lfd);
  }
  ev_close();
}
