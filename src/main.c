#include <stdio.h>
#include <stdlib.h>

#include "server.h"
#include "httpd.h"

int main(int argc, char *argv[])
{
  int port = DEFAULT_PORT;

  if (argc > 1)
  {
    port = atoi(argv[1]);
    if (port <= 0 || port > 65535)
    {
      fprintf(stderr, "invalid port: %s\n", argv[1]);
      return 1;
    }
  }

  int listen_fd = server_listen(port);
  if (listen_fd < 0)
  {
    perror("server_listen");
    return 1;
  }

  printf("httpd-c listening on port %d\n", port);
  server_run(listen_fd);

  return 0;
}