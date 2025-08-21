// client.c — send one request to the server and print response
// Usage:
//   ./client <host> <port> "<ALGO and params>"
//   ./client 127.0.0.1 5555 "MST GRAPH 5 6 -p" <<'EOF'
//   0 1 3
//   1 2 5
//   2 3 2
//   3 4 4
//   4 0 1
//   1 3 7
//   EOF

#define _XOPEN_SOURCE 700
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static int write_all(int fd, const void *buf, size_t n) {
    const char *p=(const char*)buf; size_t left=n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; left -= (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv){
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> \"<ALGO and params>\"\n", argv[0]);
        return 2;
    }
    const char *host = argv[1], *port = argv[2], *header = argv[3];

    // Resolve
    struct addrinfo hints, *res=NULL, *rp=NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family   = AF_UNSPEC;

    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); return 1; }

    int s = -1;
    for (rp = res; rp; rp = rp->ai_next) {
        s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s < 0) continue;
        if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) break;
        close(s); s = -1;
    }
    freeaddrinfo(res);
    if (s < 0) { perror("connect"); return 1; }

    // Send header line
    char firstline[1024];
    snprintf(firstline, sizeof(firstline), "%s\n", header);
    if (write_all(s, firstline, strlen(firstline)) != 0) { perror("write header"); close(s); return 1; }

    // Forward extra lines only if stdin is NOT a TTY (e.g., here-doc or pipe)
    if (!isatty(STDIN_FILENO)) {
        char ibuf[4096];
        ssize_t r;
        while ((r = read(STDIN_FILENO, ibuf, sizeof(ibuf))) > 0) {
            if (write_all(s, ibuf, (size_t)r) != 0) { perror("write payload"); close(s); return 1; }
        }
    }

    // Signal EOF to server (no more request data)
    shutdown(s, SHUT_WR);

    // Read and print server response
    char obuf[4096];
    for (;;) {
        ssize_t n = read(s, obuf, sizeof(obuf));
        if (n == 0) break;
        if (n < 0) { if (errno == EINTR) continue; perror("read"); break; }
        fwrite(obuf, 1, (size_t)n, stdout);
    }
    close(s);
    return 0;
}