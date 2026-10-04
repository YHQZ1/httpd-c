#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "event.h"
#include "httpd.h"
#include "metrics.h"
#include "router.h"
#include "server.h"
#include "static_files.h"

static void on_signal(int sig)
{
  (void)sig;
  server_request_shutdown();
}

static int parse_port(const char *s)
{
  char *end;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > 65535)
  {
    return -1;
  }
  return (int)v;
}

int main(int argc, char *argv[])
{
  // Config: argv[1] > $PORT > default. Also $WWW_ROOT and $APP_VERSION.
  const char *port_str = argc > 1 ? argv[1] : getenv("PORT");
  int port = DEFAULT_PORT;
  if (port_str && (port = parse_port(port_str)) < 0)
  {
    fprintf(stderr, "invalid port: %s\n", port_str);
    return 1;
  }

  const char *root = getenv("WWW_ROOT");
  if (!root || !*root)
  {
    root = DEFAULT_WWW_ROOT;
  }
  const char *version = getenv("APP_VERSION");
  if (!version || !*version)
  {
    version = HTTPD_VERSION;
  }

  // Line-buffer stdout so access logs show up immediately in pipes, files and
  // container log drivers.
  setvbuf(stdout, NULL, _IOLBF, 0);

  // A client hanging up mid-write must not kill the server. Signals for
  // shutdown deliberately omit SA_RESTART so a blocked wait returns promptly.
  signal(SIGPIPE, SIG_IGN);
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);

  metrics_init(version);
  router_init(version);
  if (static_init(root) < 0)
  {
    fprintf(stderr, "cannot use WWW_ROOT '%s' as a directory\n", root);
    return 1;
  }

  int listen_fd = server_listen(port);
  if (listen_fd < 0)
  {
    perror("server_listen");
    return 1;
  }

  printf("httpd-c %s listening on port %d (root=%s, backend=%s)\n", version,
         port, root, ev_backend());
  server_run(listen_fd);
  printf("shutdown complete\n");
  return 0;
}
