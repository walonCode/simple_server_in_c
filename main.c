#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(){
    int listen_fd = create_listen_socket(8080);

    while(1){
        int client_fd = accept_client(listen_fd);
        if(client_fd < 0) continue;

        char buf[1024];
        ssize_t n = read_from_client(client_fd, buf, sizeof(buf) - 1);
        if(n > 0){
            buf[n] = '\0';
            printf("Received:\n%s\n", buf);
        }

        // respond to client
        response_to_client(client_fd);
        // do NOT close(client_fd) here; already closed in response
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
