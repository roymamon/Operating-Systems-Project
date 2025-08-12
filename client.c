// client.c — sends one Euler circuit request to the server and prints the reply.
//   ./client <host> <port> <edges> <vertices> <seed> [-p]


#define _XOPEN_SOURCE 700
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char *)buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "Usage: %s <host> <port> <edges> <vertices> <seed> [-p]\n", argv[0]);
        return 2;
    }

    const char *host = argv[1];
    const char *port = argv[2];
    const char *edges = argv[3];
    const char *verts = argv[4];
    const char *seed  = argv[5];
    bool want_print = (argc == 7 && strcmp(argv[6], "-p") == 0);

    // Resolve
    struct addrinfo hints, *res = NULL, *rp = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;

    int gai = getaddrinfo(host, port, &hints, &res);
    if (gai != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai));
        return 1;
    }

    int s = -1;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s < 0) continue;
        if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(s);
        s = -1;
    }
    freeaddrinfo(res);

    if (s < 0) {
        perror("connect");
        return 1;
    }

    char line[512];
    if (want_print)
        snprintf(line, sizeof(line), "%s %s %s -p\n", edges, verts, seed);
    else
        snprintf(line, sizeof(line), "%s %s %s\n", edges, verts, seed);

    if (write_all(s, line, strlen(line)) != 0) {
        perror("write");
        close(s);
        return 1;
    }

    // Read and print server response
    char buf[4096];
    for (;;) {
        ssize_t r = read(s, buf, sizeof(buf));
        if (r == 0) break;       // server closed
        if (r < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        fwrite(buf, 1, (size_t)r, stdout);
    }

    close(s);
    return 0;
}