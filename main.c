#include "./cmd/server.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(){
    printf("server is running on http://localhost:8080\n");
    int listen_fd = create_listen_socket(8080);

    while(1){
        int client_fd = accept_client(listen_fd);
        if(client_fd < 0) continue;

        char buf[BUFFER];
        ssize_t n = read_from_client(client_fd, buf, sizeof(buf) - 1);
        if(n > 0){
            // do not read again here; pass buf + n to handler
            printf("Received (raw):\n%.*s\n", (int)n, buf);
        } else {
            close(client_fd);
            continue;
        }

        // response_to_get will handle GET/POST and close client_fd
        response_to_get(client_fd, buf, n);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
