// server.c — Factory + Strategy based graph server
// Request line: <ALGO> <edges> <vertices> <seed> [-p]\n
//   ALGO ∈ {EULER, MST, MAXCLIQUE, COUNTCLQ3P, HAMILTON}

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
#include "algo.h"

#define BACKLOG 16
#define MAX_LINE 8192

/* ---- I/O helpers ---- */
static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char*)buf; size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        p += w; left -= (size_t)w;
    }
    return 0;
}
static void sendf_fd(int fd, const char *fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    (void)write_all(fd, buf, strlen(buf));
}
static int emit_to_fd(void *ctx, const char *text) {
    int fd = *(int*)ctx;
    return write_all(fd, text, strlen(text));
}

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c; ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) break;
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        buf[i++] = c; if (c == '\n') break;
    }
    buf[i] = '\0'; return (ssize_t)i;
}

/* ---- parsing helpers ---- */
static bool parse_int(const char *s, int *out) {
    char *e = NULL; long v = strtol(s, &e, 10);
    if (e == s || *e != '\0') return false;
    if (v < INT_MIN || v > INT_MAX) return false;
    *out = (int)v; return true;
}
static bool parse_uint(const char *s, unsigned int *out) {
    char *e = NULL; unsigned long v = strtoul(s, &e, 10);
    if (e == s || *e != '\0') return false;
    if (v > 0xFFFFFFFFUL) return false;
    *out = (unsigned int)v; return true;
}

/* ---- main ---- */
int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "Usage: %s <port>\n", argv[0]); return 2; }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 2; }

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); return 1; }
    int yes = 1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); close(sfd); return 1; }
    if (listen(sfd, BACKLOG) < 0) { perror("listen"); close(sfd); return 1; }

    fprintf(stderr, "server listening on port %d\n", port);

    for (;;) {
        int cfd = accept(sfd, NULL, NULL);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }

        char line[MAX_LINE];
        if (read_line(cfd, line, sizeof(line)) <= 0) { close(cfd); continue; }

        // tokenize
        char *tok[8]; int ntok = 0;
        for (char *p = strtok(line, " \t\r\n"); p && ntok < 8; p = strtok(NULL, " \t\r\n"))
            tok[ntok++] = p;

        if (ntok < 4 || ntok > 5) {
            sendf_fd(cfd, "ERR usage: <ALGO> <edges> <vertices> <seed> [-p]\n");
            close(cfd); continue;
        }

        const char *cmd = tok[0];
        const AlgoStrategy *S = make_strategy(cmd);
        if (!S) {
            sendf_fd(cfd, "ERR unknown ALGO. Supported: EULER MST MAXCLIQUE COUNTCLQ3P HAMILTON\n");
            close(cfd); continue;
        }

        int E, V; unsigned int seed; bool want_print = false;
        if (!parse_int(tok[1], &E) || !parse_int(tok[2], &V) || !parse_uint(tok[3], &seed)) {
            sendf_fd(cfd, "ERR bad params. Usage: <ALGO> <edges> <vertices> <seed> [-p]\n");
            close(cfd); continue;
        }
        if (ntok == 5) {
            if (strcmp(tok[4], "-p") != 0) {
                sendf_fd(cfd, "ERR bad flag. Use -p or omit.\n");
                close(cfd); continue;
            }
            want_print = true;
        }

        if (V < 1 || E < 0) {
            sendf_fd(cfd, "ERR invalid: V >= 1, E >= 0\n");
            close(cfd); continue;
        }
        long long maxE = (long long)V * (V - 1) / 2;
        if ((long long)E > maxE) {
            sendf_fd(cfd, "ERR invalid: E <= V*(V-1)/2 (max=%lld)\n", maxE);
            close(cfd); continue;
        }

        Graph *g = create_graph(V);
        generate_random_graph(g, E, seed);

        if (want_print) {
            sendf_fd(cfd, "Graph: V=%d, E=%d\nAdjacency matrix:\n", g->V, g->E);
            for (int i = 0; i < g->V; ++i) {
                for (int j = 0; j < g->V; ++j) sendf_fd(cfd, "%d ", g->adj[i][j]);
                sendf_fd(cfd, "\n");
            }
        }

        // Strategy execution
        (S->run)(g, emit_to_fd, (void*)&cfd);

        free_graph(g);
        close(cfd);
    }

    close(sfd);
    return 0;
}