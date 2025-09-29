#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "server.h"

int create_listen_socket(int port){
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt");
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if(bind(listenfd, (struct sockaddr*)&address, sizeof(address)) < 0){
        perror("bind");
        exit(EXIT_FAILURE);
    }

    if(listen(listenfd, BACKLOG) < 0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    return listenfd;
}

int accept_client(int listen_fd){
    struct sockaddr_in client;
    socklen_t len = sizeof(client);

    int client_fd = accept(listen_fd, (struct sockaddr*)&client, &len);
    if(client_fd < 0){
        perror("accept");
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client.sin_addr, ip, INET_ADDRSTRLEN);
    printf("Accepted connection from: %s:%d\n", ip, ntohs(client.sin_port));

    return client_fd;
}


ssize_t read_from_client(int client_fd, char *buf, size_t len){
    ssize_t total = 0, n;
    while((n = read(client_fd, buf + total, len - total)) > 0){
        total += n;
        if(total >= (ssize_t)len) break;
        // stop if end of HTTP headers reached
        if(strstr(buf, "\r\n\r\n")) break;
    }
    return total;
}

void response_to_client(int client_fd, char *message ){
    write(client_fd, message, strlen(message) - 1);
    close(client_fd);
}

char* read_file(const char* path, size_t* out_size){
    FILE *fp = fopen(path, "r+");
    if(!fp){
        perror("fopen");
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *buffer = malloc(size + 1);
    if(!buffer){
        perror("malloc");
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buffer, 1, size, fp);
    buffer[n] = '\0';

    fclose(fp);

    if(out_size)*out_size = n;

    return buffer;
}

void response_file(int client_fd, const char* body, size_t body_len){
    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", body_len
    );

    write(client_fd, header, n);
    write(client_fd, body, body_len);
    close(client_fd);
}


void response_to_get(int client_fd, char* buf, ssize_t n){
    buf[n] = '\0';
    printf("recieved the request-----");

    char *message =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 25\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello world, Welcome Home\r\n";

    char *notfound =
    "HTTP/1.0 404 Not Found \r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

    if(strncmp(buf, "GET /home ", 10) == 0){
        response_to_client(client_fd, message);
    }else if(strncmp(buf, "GET /home.html ", 15) == 0){
        size_t file_size;
        char *body = read_file("./static/index.html", &file_size);
        response_file(client_fd, body, file_size);
    }
    else{
        response_to_client(client_fd, notfound);
    }
}
