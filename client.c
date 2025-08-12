// client.c — sends "<edges> <vertices> <seed>\n" and prints server reply.
// Usage: ./client <server_ip_or_host> <port> <edges> <vertices> <seed>
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <server_host> <port> <edges> <vertices> <seed>\n", argv[0]);
        return 2;
    }

    const char *host = argv[1];
    const char *port = argv[2];
    const char *edges = argv[3];
    const char *verts = argv[4];
    const char *seed  = argv[5];

    // Resolve
    struct addrinfo hints = {0}, *res = NULL, *rp = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc));
        return 1;
    }

    int s = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s < 0) continue;
        if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(s); s = -1;
    }
    freeaddrinfo(res);

    if (s < 0) { perror("connect"); return 1; }

    char line[128];
    snprintf(line, sizeof(line), "%s %s %s\n", edges, verts, seed);
    if (send_all(s, line, strlen(line)) < 0) { perror("write"); close(s); return 1; }

    // Read till EOF and print
    char buf[4096];
    ssize_t n;
    while ((n = read(s, buf, sizeof(buf))) > 0) {
        fwrite(buf, 1, (size_t)n, stdout);
    }
    close(s);
    return 0;
}