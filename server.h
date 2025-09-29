#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>

#define BACKLOG 10
int create_listen_socket(int port);
int accept_client(int listen_fd);
ssize_t read_from_client(int client_fd, char *buf, size_t len);
void response_to_client(int client_fd);

#endif
