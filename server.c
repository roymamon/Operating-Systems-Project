// server.c — TCP server for Euler circuit requests.
//   "<edges> <vertices> <seed> [-p]\n"

#define _XOPEN_SOURCE 700
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "graph.h"

#define BACKLOG 16
#define MAX_LINE 8192

/* ---------- I/O helpers ---------- */

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

static int sendf(int fd, const char *fmt, ...) {
    char stackbuf[4096];
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);

    if (need < 0) return -1;
    if ((size_t)need < sizeof(stackbuf)) {
        return write_all(fd, stackbuf, (size_t)need);
    }

    // Large line fallback
    size_t cap = (size_t)need + 1;
    char *heapbuf = (char *)malloc(cap);
    if (!heapbuf) return -1;
    va_start(ap, fmt);
    vsnprintf(heapbuf, cap, fmt, ap);
    va_end(ap);
    int rc = write_all(fd, heapbuf, strlen(heapbuf));
    free(heapbuf);
    return rc;
}

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) break;               // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

/* ---------- parsing helpers ---------- */

static bool parse_int(const char *s, int *out) {
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v < INT_MIN || v > INT_MAX) return false;
    *out = (int)v;
    return true;
}

static bool parse_uint(const char *s, unsigned int *out) {
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v > 0xFFFFFFFFUL) return false;
    *out = (unsigned int)v;
    return true;
}

/* ---------- server main ---------- */

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 2;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Invalid port\n");
        return 2;
    }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd);
        return 1;
    }
    if (listen(sfd, BACKLOG) < 0) {
        perror("listen");
        close(sfd);
        return 1;
    }

    fprintf(stderr, "server listening on port %d\n", port);

    for (;;) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        char line[MAX_LINE];
        ssize_t n = read_line(cfd, line, sizeof(line));
        if (n <= 0) { close(cfd); continue; }

        // Tokenize the line
        char *tok[8];
        int ntok = 0;
        for (char *p = strtok(line, " \t\r\n"); p && ntok < 8; p = strtok(NULL, " \t\r\n")) {
            tok[ntok++] = p;
        }

        bool want_print = false;
        if (ntok == 4) {
            if (strcmp(tok[3], "-p") != 0) {
                sendf(cfd, "ERR invalid request. Use: \"<edges> <vertices> <seed> [-p]\\n\".\n");
                close(cfd);
                continue;
            }
            want_print = true;
        } else if (ntok != 3) {
            sendf(cfd, "ERR invalid request. Use: \"<edges> <vertices> <seed> [-p]\\n\".\n");
            close(cfd);
            continue;
        }

        int E, V;
        unsigned int seed;
        if (!parse_int(tok[0], &E) || !parse_int(tok[1], &V) || !parse_uint(tok[2], &seed)) {
            sendf(cfd, "ERR invalid request. Use: \"<edges> <vertices> <seed> [-p]\\n\".\n");
            close(cfd);
            continue;
        }

        if (V < 1 || E < 0) {
            sendf(cfd, "ERR invalid params: E >= 0, V >= 1.\n");
            close(cfd);
            continue;
        }
        long long maxE = (long long)V * (V - 1) / 2;
        if ((long long)E > maxE) {
            sendf(cfd, "ERR invalid params: E <= V*(V-1)/2 (max=%lld).\n", maxE);
            close(cfd);
            continue;
        }

        Graph *g = create_graph(V);
        generate_random_graph(g, E, seed);

        if (want_print) {
            sendf(cfd, "Graph: V=%d, E=%d\nAdjacency matrix:\n", g->V, g->E);
            for (int i = 0; i < g->V; ++i) {
                for (int j = 0; j < g->V; ++j)
                    sendf(cfd, "%d ", g->adj[i][j]);
                sendf(cfd, "\n");
            }
        }

        if (!connected_among_non_isolated(g)) {
            sendf(cfd, "No Euler circuit: graph is disconnected among non-isolated vertices.\n");
            free_graph(g);
            close(cfd);
            continue;
        }

        int oddCount = 0;
        for (int i = 0; i < g->V; ++i) if (degree(g, i) % 2 != 0) oddCount++;
        if (oddCount != 0) {
            sendf(cfd, "No Euler circuit: %d vertices have odd degree.\n", oddCount);
            free_graph(g);
            close(cfd);
            continue;
        }

        int *path = NULL, pathLen = 0;
        if (!euler_circuit(g, &path, &pathLen)) {
            sendf(cfd, "No Euler circuit (unexpected after checks).\n");
            free_graph(g);
            close(cfd);
            continue;
        }

        sendf(cfd, "Euler circuit exists. Sequence of vertices:\n");
        for (int i = 0; i < pathLen; ++i) {
            sendf(cfd, "%d%s", path[i], (i + 1 == pathLen ? "\n" : " -> "));
        }

        free(path);
        free_graph(g);
        close(cfd);
    }

    close(sfd);
    return 0;
}