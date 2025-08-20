// client.c — send one request line to the server and print response
// Usage: ./client <host> <port> "<ALGO> <edges> <vertices> <seed> [-p]>"

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
    while(left){ ssize_t w=write(fd,p,left); if(w<0){ if(errno==EINTR) continue; return -1; } p+=w; left-=w; }
    return 0;
}

int main(int argc, char **argv){
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> \"<ALGO> <edges> <vertices> <seed> [-p]\"\n", argv[0]);
        return 2;
    }
    const char *host = argv[1], *port = argv[2], *line = argv[3];

    struct addrinfo hints, *res=NULL, *rp=NULL;
    memset(&hints, 0, sizeof(hints)); hints.ai_socktype = SOCK_STREAM; hints.ai_family=AF_UNSPEC;
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

    char sendline[1024];
    snprintf(sendline, sizeof(sendline), "%s\n", line);
    if (write_all(s, sendline, strlen(sendline)) != 0) { perror("write"); close(s); return 1; }

    char buf[4096];
    for (;;) {
        ssize_t r = read(s, buf, sizeof(buf));
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; perror("read"); break; }
        fwrite(buf, 1, (size_t)r, stdout);
    }
    close(s);
    return 0;
}