#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "event.h"
#include "httpd.h"

#ifdef __linux__

#include <sys/epoll.h>

static int epfd = -1;

static uint32_t mask(int r, int w)
{
  return (r ? EPOLLIN : 0) | (w ? EPOLLOUT : 0);
}

const char *ev_backend(void) { return "epoll"; }

int ev_init(void)
{
  epfd = epoll_create1(0);
  return epfd < 0 ? -1 : 0;
}

void ev_close(void)
{
  if (epfd >= 0)
  {
    close(epfd);
    epfd = -1;
  }
}

int ev_add(int fd, int r, int w)
{
  struct epoll_event e;
  memset(&e, 0, sizeof(e));
  e.events = mask(r, w);
  e.data.fd = fd;
  return epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &e);
}

int ev_mod(int fd, int r, int w)
{
  struct epoll_event e;
  memset(&e, 0, sizeof(e));
  e.events = mask(r, w);
  e.data.fd = fd;
  return epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &e);
}

void ev_del(int fd)
{
  struct epoll_event e;
  memset(&e, 0, sizeof(e));
  epoll_ctl(epfd, EPOLL_CTL_DEL, fd, &e);
}

int ev_wait(ev_event *out, int max, int timeout_ms)
{
  struct epoll_event evs[EV_MAX_EVENTS];
  if (max > EV_MAX_EVENTS)
  {
    max = EV_MAX_EVENTS;
  }

  int n = epoll_wait(epfd, evs, max, timeout_ms);
  if (n < 0)
  {
    return errno == EINTR ? 0 : -1;
  }

  for (int i = 0; i < n; i++)
  {
    out[i].fd = evs[i].data.fd;
    out[i].readable = (evs[i].events & EPOLLIN) != 0;
    out[i].writable = (evs[i].events & EPOLLOUT) != 0;
    out[i].error = (evs[i].events & (EPOLLERR | EPOLLHUP)) != 0;
  }
  return n;
}

#else // portable poll() fallback (macOS, BSD)

#include <poll.h>

static struct pollfd pfds[MAX_FDS];
static int idx[MAX_FDS]; // fd -> index in pfds, or -1
static int nfds;
static int rotate; // start offset, so low fds can't starve high ones

static short mask(int r, int w)
{
  return (short)((r ? POLLIN : 0) | (w ? POLLOUT : 0));
}

const char *ev_backend(void) { return "poll"; }

int ev_init(void)
{
  nfds = 0;
  for (int i = 0; i < MAX_FDS; i++)
  {
    idx[i] = -1;
  }
  return 0;
}

void ev_close(void) { nfds = 0; }

int ev_add(int fd, int r, int w)
{
  if (fd < 0 || fd >= MAX_FDS || idx[fd] >= 0)
  {
    errno = EINVAL;
    return -1;
  }
  pfds[nfds].fd = fd;
  pfds[nfds].events = mask(r, w);
  pfds[nfds].revents = 0;
  idx[fd] = nfds++;
  return 0;
}

int ev_mod(int fd, int r, int w)
{
  if (fd < 0 || fd >= MAX_FDS || idx[fd] < 0)
  {
    errno = EINVAL;
    return -1;
  }
  pfds[idx[fd]].events = mask(r, w);
  return 0;
}

void ev_del(int fd)
{
  if (fd < 0 || fd >= MAX_FDS || idx[fd] < 0)
  {
    return;
  }
  int i = idx[fd];
  int last = nfds - 1;
  pfds[i] = pfds[last];
  idx[pfds[i].fd] = i;
  idx[fd] = -1;
  nfds--;
}

int ev_wait(ev_event *out, int max, int timeout_ms)
{
  int n = poll(pfds, (nfds_t)nfds, timeout_ms);
  if (n < 0)
  {
    return errno == EINTR ? 0 : -1;
  }

  int count = 0;
  rotate++;
  for (int k = 0; k < nfds && count < max; k++)
  {
    struct pollfd *p = &pfds[(rotate + k) % nfds];
    if (p->revents == 0)
    {
      continue;
    }
    out[count].fd = p->fd;
    out[count].readable = (p->revents & POLLIN) != 0;
    out[count].writable = (p->revents & POLLOUT) != 0;
    out[count].error = (p->revents & (POLLERR | POLLHUP | POLLNVAL)) != 0;
    count++;
  }
  return count;
}

#endif
