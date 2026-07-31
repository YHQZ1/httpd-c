#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server.h"
#include "httpd.h"

static const char *V1_RESPONSE =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 13\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello, world!";

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

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons((uint16_t)port);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
  {
    close(fd);
    return -1;
  }

  if (listen(fd, BACKLOG) < 0)
  {
    close(fd);
    return -1;
  }

  return fd;
}

static void handle_connection(int client_fd, struct sockaddr_in *peer_addr)
{
  char req_buf[READ_BUF_SIZE];
  ssize_t n = read(client_fd, req_buf, sizeof(req_buf) - 1);

  if (n < 0)
  {
    perror("read");
    close(client_fd);
    return;
  }
  req_buf[n] = '\0';

  char peer_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, &peer_addr->sin_addr, peer_ip, sizeof(peer_ip));
  printf("[%s:%d] %.*s\n", peer_ip, ntohs(peer_addr->sin_port),
         (int)strcspn(req_buf, "\r\n"), req_buf);

  ssize_t resp_len = (ssize_t)strlen(V1_RESPONSE);
  ssize_t written = 0;
  while (written < resp_len)
  {
    ssize_t w = write(client_fd, V1_RESPONSE + written, resp_len - written);
    if (w < 0)
    {
      perror("write");
      break;
    }
    written += w;
  }

  close(client_fd);
}

void server_run(int listen_fd)
{
  for (;;)
  {
    struct sockaddr_in peer_addr;
    socklen_t peer_len = sizeof(peer_addr);

    int client_fd = accept(listen_fd, (struct sockaddr *)&peer_addr, &peer_len);
    if (client_fd < 0)
    {
      perror("accept");
      continue;
    }

    handle_connection(client_fd, &peer_addr);
  }
}