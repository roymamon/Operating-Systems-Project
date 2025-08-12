// server.c — TCP server that answers Euler-circuit requests using graph.c logic.
// Usage: ./server <port>
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "graph.h"

static int send_all(int fd, const char *buf, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        off += (size_t)n;
    }
    return 0;
}

static int read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n == 0) break;          // EOF
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') break;
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 2;
    }

    int port = atoi(argv[1]);
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s);
        return 1;
    }
    if (listen(s, 16) < 0) {
        perror("listen");
        close(s);
        return 1;
    }

    printf("Server listening on port %d...\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in cli;
        socklen_t clilen = sizeof(cli);
        int cfd = accept(s, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char line[256];
        int n = read_line(cfd, line, sizeof(line));
        if (n <= 0) { close(cfd); continue; }

        // Format: "E V S" (edges, vertices, seed)
        int E = -1, V = -1;
        unsigned int seed = 0;
        if (sscanf(line, "%d %d %u", &E, &V, &seed) != 3 || V < 1 || E < 0) {
            const char *msg = "ERR invalid request. Use: \"<edges> <vertices> <seed>\\n\".\n";
            send_all(cfd, msg, strlen(msg));
            close(cfd);
            continue;
        }

        Graph *g = create_graph(V);
        generate_random_graph(g, E, seed);

        // Prepare a response
        char *resp = NULL;
        size_t cap = 4096, off = 0;
        resp = malloc(cap);
        if (!resp) { perror("malloc"); close(cfd); free_graph(g); continue; }

        // Connectivity + degree parity
        if (!connected_among_non_isolated(g)) {
            off += snprintf(resp + off, cap - off,
                            "No Euler circuit: graph disconnected among non-isolated vertices.\n");
        } else {
            int odd = 0;
            for (int i = 0; i < g->V; ++i) if (degree(g, i) % 2) odd++;
            if (odd != 0) {
                off += snprintf(resp + off, cap - off,
                                "No Euler circuit: %d vertices have odd degree.\n", odd);
            } else {
                int *path = NULL, pathLen = 0;
                if (euler_circuit(g, &path, &pathLen)) {
                    off += snprintf(resp + off, cap - off,
                                    "Euler circuit exists. Vertex sequence:\n");
                    for (int i = 0; i < pathLen; ++i) {
                        if (off + 32 >= cap) { cap *= 2; resp = realloc(resp, cap); }
                        off += snprintf(resp + off, cap - off, "%d%s",
                                        path[i], (i + 1 == pathLen ? "\n" : " -> "));
                    }
                    free(path);
                } else {
                    off += snprintf(resp + off, cap - off, "No Euler circuit.\n");
                }
            }
        }

        send_all(cfd, resp, off);
        free(resp);
        free_graph(g);
        close(cfd);
    }

    close(s);
    return 0;
}