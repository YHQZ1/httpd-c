#ifndef SERVER_H
#define SERVER_H

int server_listen(int port);
void server_run(int listen_fd);

#endif // SERVER_H