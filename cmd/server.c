#include "server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

// This function is use to created a socket and returnn the socket
int create_listen_socket(int port){
    //create socket
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if(listenfd < 0){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    //tell the os to bind this socket immediately
    if(setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt");
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    //bind socket to the created sockaddr_in
    if(bind(listenfd, (struct sockaddr*)&address, sizeof(address)) < 0){
        perror("bind");
        exit(EXIT_FAILURE);
    }

    //we are listening to incomming messages
    if(listen(listenfd, BACKLOG) < 0){
        perror("listen");
        exit(EXIT_FAILURE);
    }

    //returns the socket
    return listenfd;
}

//accept incoming message
int accept_client(int listen_fd){
    struct sockaddr_in client;
    socklen_t len = sizeof(client);

    //accept the client
    int client_fd = accept(listen_fd, (struct sockaddr*)&client, &len);
    if(client_fd < 0){
        perror("accept");
        return -1;
    }

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client.sin_addr, ip, INET_ADDRSTRLEN);
    printf("Accepted connection from: %s:%d\n", ip, ntohs(client.sin_port));

    //return the client_fd
    return client_fd;
}


//read the header from the request
ssize_t read_from_client(int client_fd, char *buf, size_t len){
    ssize_t total = 0, n;
    while((n = read(client_fd, buf + total, (ssize_t)len - total)) > 0){
        total += n;
        if(total >= (ssize_t)len) break;
        // stop if end of HTTP headers reached (but keep any extra bytes read)
        if(strstr(buf, "\r\n\r\n")) break;
    }
    return total;
}


//helper function to response to the client
void response_to_client(int client_fd, const char *message ){
    if (!message) {
        close(client_fd);
        return;
    }
    // write full length (no -1!). Use strlen(message)
    ssize_t to_write = (ssize_t)strlen(message);
    ssize_t written = 0;
    while (written < to_write) {
        ssize_t w = write(client_fd, message + written, (size_t)(to_write - written));
        if (w <= 0) {
            if (errno == EINTR) continue;
            break;
        }
        written += w;
    }
    close(client_fd);
}

char* read_file(const char* path, size_t* out_size){
    FILE *fp = fopen(path, "rb"); // binary read
    if(!fp){
        perror("fopen");
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        perror("fseek");
        fclose(fp);
        return NULL;
    }

    long size = ftell(fp);
    if (size < 0) {
        perror("ftell");
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buffer = malloc((size_t)size + 1);
    if(!buffer){
        perror("malloc");
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buffer, 1, (size_t)size, fp);
    buffer[n] = '\0';

    fclose(fp);

    if(out_size) *out_size = n;

    return buffer;
}

void response_file(int client_fd, const char* body, size_t body_len){
    if (!body) {
        // file missing; send 404
        const char notfound[] =
            "HTTP/1.0 404 Not Found\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: 9\r\n"
            "Connection: close\r\n"
            "\r\n"
            "Not Found";
        response_to_client(client_fd, notfound);
        return;
    }

    char header[512];
    int n = snprintf(header, sizeof(header),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", body_len
    );

    // send header + body robustly
    ssize_t written = 0;
    while (written < n) {
        ssize_t w = write(client_fd, header + written, (size_t)(n - written));
        if (w <= 0) { if (errno == EINTR) continue; break; }
        written += w;
    }
    written = 0;
    while ((size_t)written < body_len) {
        ssize_t w = write(client_fd, body + written, (size_t)(body_len - written));
        if (w <= 0) { if (errno == EINTR) continue; break; }
        written += w;
    }
    close(client_fd);
}

/*
 Parse Content-Length from headers buffer in a robust way.
 Returns -1 on error, or the parsed non-negative length.
*/
static long parse_content_length(const char *headers) {
    if (!headers) return -1;
    const char *p = headers;
    while ((p = strcasestr(p, "Content-Length")) != NULL) {
        // find the colon
        const char *colon = strchr(p, ':');
        if (!colon) { p += 14; continue; }
        const char *num = colon + 1;
        // skip spaces
        while (*num && isspace((unsigned char)*num)) ++num;
        if (!*num) return -1;
        errno = 0;
        char *endptr = NULL;
        long v = strtol(num, &endptr, 10);
        if (endptr == num) return -1;
        if (errno != 0) return -1;
        if (v < 0) return -1;
        return v;
    }
    return 0; // not present -> 0
}

/*
 Read POST body, given headers already present in headers_buf.
 headers_len is number of bytes currently in headers_buf (may include some of the body).
 On success: *body_out is malloc'd, *body_len_out set, return 0.
 On failure: return -1.
*/
int read_post_body(int client_fd, char *headers_buf, ssize_t headers_len, char **body_out, size_t *body_len_out) {
    if (!headers_buf || headers_len <= 0 || !body_out) return -1;
    headers_buf[headers_len] = '\0';

    char *hdr_end = strstr(headers_buf, "\r\n\r\n");
    if (!hdr_end) return -1; // caller must ensure headers are complete

    // parse content-length
    long content_length = parse_content_length(headers_buf);
    if (content_length < 0) return -1;

    char *body_start = hdr_end + 4;
    ssize_t already = headers_len - (body_start - headers_buf); // may be 0 or >0
    if (already < 0) already = 0;

    // allocate
    size_t total = (size_t)content_length;
    char *body = malloc(total + 1);
    if (!body) return -1;

    if ((size_t)already > 0) {
        memcpy(body, body_start, (size_t)already);
    }

    size_t to_read = (total > (size_t)already) ? (total - (size_t)already) : 0;
    size_t got = (size_t)already;
    while (got < total && to_read > 0) {
        ssize_t r = read(client_fd, body + got, to_read);
        if (r < 0) {
            if (errno == EINTR) continue;
            free(body);
            return -1;
        }
        if (r == 0) break; // client closed early
        got += (size_t)r;
        to_read = total - got;
    }

    body[total] = '\0';
    *body_out = body;
    if (body_len_out) *body_len_out = total;
    return 0;
}

User user;

void save_data_from_request(const char *body) {
    char name[128] = "";
    char age_str[16] = "";

    // naive parsing, assumes "name=...&age=..."
    const char *p_name = strstr(body, "name=");
    const char *p_age = strstr(body, "age=");

    if (p_name) {
        p_name += strlen("name=");
        const char *amp = strchr(p_name, '&');
        if (amp) {
            strncpy(name, p_name, amp - p_name);
            name[amp - p_name] = '\0';
        } else {
            strncpy(name, p_name, sizeof(name)-1);
            name[sizeof(name)-1] = '\0';
        }
    }

    if (p_age) {
        p_age += strlen("age=");
        const char *amp = strchr(p_age, '&');
        if (amp) {
            strncpy(age_str, p_age, amp - p_age);
            age_str[amp - p_age] = '\0';
        } else {
            strncpy(age_str, p_age, sizeof(age_str)-1);
            age_str[sizeof(age_str)-1] = '\0';
        }
    }

    // store in your global user struct
    strncpy(user.name, name, sizeof(user.name)-1);
    user.name[sizeof(user.name)-1] = '\0';

    user.age = atoi(age_str);  // convert string to int
}

void response_to_get(int client_fd, char* buf, ssize_t n){
    if (n <= 0) { close(client_fd); return; }
    buf[n] = '\0';
    printf("Received request-----\n%s\n", buf);

    const char *message =
    "HTTP/1.0 200 OK\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 25\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Hello world, Welcome Home\r\n";

    const char *notfound =
    "HTTP/1.0 404 Not Found\r\n"
    "Content-Type: text/plain\r\n"
    "Content-Length: 9\r\n"
    "Connection: close\r\n"
    "\r\n"
    "Not Found";

    if(strncmp(buf, "GET /home ", 10) == 0){
        response_to_client(client_fd, message);
    } else if(strncmp(buf, "GET /home.html ", 15) == 0){
        size_t file_size;
        char *body = read_file("./static/index.html", &file_size);
        if (body) {
            response_file(client_fd, body, file_size);
            free(body);
        } else {
            response_to_client(client_fd, notfound);
        }
    } else if (strncmp(buf, "POST /users ", 12) == 0 || strncmp(buf, "POST /users HTTP/1.1", 20) == 0){
        // We already have headers+maybe body in buf; use read_post_body to get body fully.
        char *body = NULL;
        size_t body_len = 0;
        if (read_post_body(client_fd, buf, n, &body, &body_len) == 0) {
            printf("POST body: (%zu bytes) %s\n", body_len, body ? body : "(null)");
            // respond with a success message (compute Content-Length automatically)
            save_data_from_request(body);
            char resp_body[1000];
            int rb = snprintf(
                resp_body,
                sizeof(resp_body),
                "{\"username\":\"%s\", \"age\":%d}\n",
                user.name,
                user.age
            );
            char header[256];
            int hn = snprintf(header, sizeof(header),
                "HTTP/1.0 200 OK\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n", rb);
            // send header
            write(client_fd, header, (size_t)hn);
            // send body
            write(client_fd, resp_body, (size_t)rb);
            free(body);
            close(client_fd);
        } else {
            // failed to read body
            response_to_client(client_fd, notfound);
        }
    }else if(strncmp(buf, "GET /users ", 11) == 0 || strncmp(buf, "GET /users HTTP/1.1 ",20 ) == 0){
        User user;
        user.age = 20;
        strcpy(user.name, "walon");

        char resp_body[1000];
        int rb  =snprintf(
            resp_body,
            sizeof(resp_body),
            "{\"username\":\"%s\", \"age\":%d}\n",
            user.name,
            user.age
        );

        char header[256];
        int hn = snprintf(header, sizeof(header),
            "HTTP/1.0 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Connection: close\r\n"
            "\r\n", rb);

        write(client_fd, header, hn);
        write(client_fd, resp_body, rb);
        close(client_fd);
    }
    else {
        response_to_client(client_fd, notfound);
    }
}
