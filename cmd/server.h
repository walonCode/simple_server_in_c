#ifndef SERVER_H
#define SERVER_H

#include <sys/types.h>

#define BACKLOG 10


int create_listen_socket(int port);
int accept_client(int listen_fd);
ssize_t read_from_client(int client_fd, char *buf, size_t len);
void response_to_client(int client_fd, char *messsage);
void response_to_get(int client_fd, char *buf, ssize_t n);
char* read_file(const char* path, size_t* out_size);
void response_file(int client_fd, const char* body, size_t body_len);


#endif
