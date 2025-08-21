// server.c — Leader–Follower multithreaded graph server
// Requests supported:
//   A) Back-compat RANDOM (single line):
//        <ALGO> <E> <V> <SEED> [-p]
//   B) Explicit GRAPH (edges follow) — NOTE: <E> <V> order (edges first):
//        <ALGO> GRAPH <E> <V> [-p]\n
//        then E lines: "u v [w]\n"  (undirected; 0<=u,v<V; weight optional -> default 1)
//
// ALGO ∈ {EULER, MST, MAXCLIQUE, COUNTCLQ3P, HAMILTON}
// Use -p to also print adjacency matrix to the client.

#define _XOPEN_SOURCE 700
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
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

#define BACKLOG   64
#define MAX_LINE  8192

/* ---------- globals ---------- */

static int g_listen_fd = -1;

/* Leader–Follower coordination */
static pthread_mutex_t lf_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lf_cv  = PTHREAD_COND_INITIALIZER;
static int             has_leader = 0;

/* Guard the non-threadsafe rand()/srand() used inside generate_random_graph() */
static pthread_mutex_t rng_mtx = PTHREAD_MUTEX_INITIALIZER;

/* ---------- small I/O helpers ---------- */

static int write_all(int fd, const void *buf, size_t n) {
    const char *p = (const char*)buf;
    size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return 0;
}

static int emit_to_fd(void *ctx, const char *text) {
    int fd = *(int*)ctx;
    return write_all(fd, text, strlen(text));
}

static void sendf_fd(int fd, const char *fmt, ...) {
    char buf[4096];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    (void)write_all(fd, buf, strlen(buf));
}

static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) break;               // peer closed
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    return (ssize_t)i;
}

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

/* ---------- tiny helper: add edge (avoid needing graph_add_edge symbol) ---------- */
static void add_edge_local(Graph *g, int u, int v, int w) {
    if (u < 0 || v < 0 || u >= g->V || v >= g->V) return;
    if (u == v) return;
    if (w <= 0) return;
    if (g->adj[u][v]) return;     // ignore duplicates
    g->adj[u][v] = g->adj[v][u] = 1;
    g->w[u][v]   = g->w[v][u]   = w;
    g->E++;
}

/* ---------- request handling ---------- */

static int print_adj_to_client(int fd, const Graph *g) {
    sendf_fd(fd, "Graph: V=%d, E=%d\nAdjacency matrix:\n", g->V, g->E);
    for (int i = 0; i < g->V; ++i) {
        for (int j = 0; j < g->V; ++j) sendf_fd(fd, "%d ", g->adj[i][j]);
        sendf_fd(fd, "\n");
    }
    return 0;
}

static void handle_client(int cfd) {
    char line[MAX_LINE];
    if (read_line(cfd, line, sizeof(line)) <= 0) return;

    // tokenize first line
    char *tok[8]; int ntok = 0;
    for (char *p = strtok(line, " \t\r\n"); p && ntok < 8; p = strtok(NULL, " \t\r\n"))
        tok[ntok++] = p;

    if (ntok < 4) {
        sendf_fd(cfd, "ERR usage:\n"
                      "  <ALGO> <E> <V> <SEED> [-p]\n"
                      "  <ALGO> GRAPH <E> <V> [-p]  (then E lines: u v [w])\n");
        return;
    }

    const char *cmd = tok[0];
    const AlgoStrategy *S = make_strategy(cmd);
    if (!S) {
        sendf_fd(cfd, "ERR unknown ALGO. Supported: EULER MST MAXCLIQUE COUNTCLQ3P HAMILTON\n");
        return;
    }

    bool want_print = false;
    bool mode_graph = false;

    int  V = -1, E = -1;
    unsigned int seed = 0;

    if (strcmp(tok[1], "GRAPH") == 0) {
        // <ALGO> GRAPH <E> <V> [-p]   <-- Edges first (as requested)
        if (ntok < 4 || ntok > 5) { sendf_fd(cfd, "ERR usage: <ALGO> GRAPH <E> <V> [-p]\n"); return; }
        if (!parse_int(tok[2], &E) || !parse_int(tok[3], &V)) { sendf_fd(cfd, "ERR bad E/V\n"); return; }
        if (ntok == 5) {
            if (strcmp(tok[4], "-p") != 0) { sendf_fd(cfd, "ERR bad flag. Use -p or omit.\n"); return; }
            want_print = true;
        }
        mode_graph = true;
    } else {
        // Backward compatible RANDOM form: <ALGO> <E> <V> <SEED> [-p]
        if (ntok < 4 || ntok > 5) { sendf_fd(cfd, "ERR usage: <ALGO> <E> <V> <SEED> [-p]\n"); return; }
        if (!parse_int(tok[1], &E) || !parse_int(tok[2], &V) || !parse_uint(tok[3], &seed)) {
            sendf_fd(cfd, "ERR bad params.\n"); return;
        }
        if (ntok == 5) {
            if (strcmp(tok[4], "-p") != 0) { sendf_fd(cfd, "ERR bad flag. Use -p or omit.\n"); return; }
            want_print = true;
        }
    }

    if (V < 1 || E < 0) { sendf_fd(cfd, "ERR invalid: V >= 1, E >= 0\n"); return; }
    long long maxE = (long long)V * (V - 1) / 2;
    if ((long long)E > maxE) { sendf_fd(cfd, "ERR invalid: E <= V*(V-1)/2 (max=%lld)\n", maxE); return; }

    Graph *g = create_graph(V);

    if (mode_graph) {
        // read E lines of "u v [w]"
        for (int i = 0; i < E; ++i) {
            char eline[256];
            if (read_line(cfd, eline, sizeof(eline)) <= 0) {
                sendf_fd(cfd, "ERR expected %d edge lines; got %d\n", E, i);
                free_graph(g); return;
            }
            char *a = strtok(eline, " \t\r\n");
            char *b = strtok(NULL, " \t\r\n");
            char *c = strtok(NULL, " \t\r\n");
            if (!a || !b) { sendf_fd(cfd, "ERR edge line format: u v [w]\n"); free_graph(g); return; }
            int u, v, w = 1;
            if (!parse_int(a, &u) || !parse_int(b, &v)) { sendf_fd(cfd, "ERR edge endpoints\n"); free_graph(g); return; }
            if (c) {
                int tmp; if (!parse_int(c, &tmp) || tmp <= 0) { sendf_fd(cfd, "ERR weight must be positive\n"); free_graph(g); return; }
                w = tmp;
            }
            if (u < 0 || u >= V || v < 0 || v >= V || u == v) { sendf_fd(cfd, "ERR invalid edge %d: (%d,%d)\n", i, u, v); free_graph(g); return; }
            add_edge_local(g, u, v, w);  // ignore duplicates
        }
    } else {
        // RANDOM generation (back-compat single line)
        pthread_mutex_lock(&rng_mtx);
        generate_random_graph(g, E, seed);
        pthread_mutex_unlock(&rng_mtx);
    }

    if (want_print) print_adj_to_client(cfd, g);

    // Run selected strategy and emit results to this client
    (S->run)(g, emit_to_fd, (void*)&cfd);

    free_graph(g);
}

/* ---------- worker thread (Leader–Follower) ---------- */

static void *worker_main(void *arg) {
    (void)arg;
    for (;;) {
        // become leader
        pthread_mutex_lock(&lf_mtx);
        while (has_leader) pthread_cond_wait(&lf_cv, &lf_mtx);
        has_leader = 1;
        pthread_mutex_unlock(&lf_mtx);

        // accept a connection
        int cfd = accept(g_listen_fd, NULL, NULL);
        int err = (cfd < 0) ? errno : 0;

        // promote next follower to leader
        pthread_mutex_lock(&lf_mtx);
        has_leader = 0;
        pthread_cond_signal(&lf_cv);
        pthread_mutex_unlock(&lf_mtx);

        if (cfd < 0) {
            if (err == EINTR) continue;
            // transient errors: just continue loop
            continue;
        }

        // handle client on this thread
        handle_client(cfd);
        close(cfd);
    }
    return NULL;
}

/* ---------- main ---------- */

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <port> [threads]\n", argv[0]);
        return 2;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 2; }

    int nthreads = 0;
    if (argc == 3) {
        nthreads = atoi(argv[2]);
    } else {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = (n > 0) ? (int)n : 4;
    }
    if (nthreads < 1) nthreads = 1;

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) { perror("socket"); return 1; }
    int yes = 1; setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(g_listen_fd, BACKLOG) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "server listening on port %d with %d threads\n", port, nthreads);

    // spawn pool
    pthread_t tid;
    for (int i = 0; i < nthreads; ++i) {
        if (pthread_create(&tid, NULL, worker_main, NULL) != 0) { perror("pthread_create"); return 1; }
        pthread_detach(tid);
    }

    // the main thread can idle forever; workers serve clients
    for (;;) pause();
    return 0;
}