//A) Back-compat RANDOM (single header line):
//<ALGO> <E> <V> <SEED> [-p]
//B) Explicit GRAPH (edges follow; NOTE: order is <E> <V>):
//<ALGO> GRAPH <E> <V> [-p]\n
//(then E lines: "u v [w]\n" ; undirected; weight optional->default 1)
//ALGO ∈ {EULER, MST, MAXCLIQUE, COUNTCLQ3P, HAMILTON}
//Use -p to also print adjacency matrix to the client.
// Run:   ./server <port> [threads]

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
static ssize_t read_line(int fd, char *buf, size_t cap) {
    size_t i = 0;
    while (i + 1 < cap) {
        char c;
        ssize_t r = recv(fd, &c, 1, 0);
        if (r == 0) break;
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

typedef struct {
    char *buf; size_t len, cap;
} StrBuf;

static void sb_init(StrBuf *s) { s->buf=NULL; s->len=0; s->cap=0; }
static void sb_free(StrBuf *s){ free(s->buf); s->buf=NULL; s->len=s->cap=0; }
static void sb_ensure(StrBuf *s, size_t need){
    if (need <= s->cap) return;
    size_t ncap = s->cap ? s->cap : 1024;
    while (ncap < need) ncap *= 2;
    char *nb = (char*)realloc(s->buf, ncap);
    if (!nb) { perror("realloc"); exit(1); }
    s->buf = nb; s->cap = ncap;
}
static void sb_append(StrBuf *s, const char *data, size_t n){
    sb_ensure(s, s->len + n + 1);
    memcpy(s->buf + s->len, data, n);
    s->len += n; s->buf[s->len] = '\0';
}
static void sb_printf(StrBuf *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char tmp[4096];
    int m = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (m < 0) return;
    if ((size_t)m < sizeof(tmp)) { sb_append(s, tmp, (size_t)m); return; }
    size_t need = (size_t)m + 1;
    char *big = (char*)malloc(need);
    if (!big) { perror("malloc"); exit(1); }
    va_start(ap, fmt);
    vsnprintf(big, need, fmt, ap);
    va_end(ap);
    sb_append(s, big, (size_t)m);
    free(big);
}

typedef struct QNode {
    void *item;
    struct QNode *next;
} QNode;

typedef struct {
    QNode *head, *tail;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
} Queue;

static void q_init(Queue *q){
    q->head = q->tail = NULL;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv, NULL);
}
static void q_push(Queue *q, void *item){
    QNode *n = (QNode*)malloc(sizeof(QNode));
    if (!n) { perror("malloc"); exit(1); }
    n->item = item; n->next = NULL;
    pthread_mutex_lock(&q->mtx);
    if (!q->tail) q->head = q->tail = n;
    else { q->tail->next = n; q->tail = n; }
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}
static void* q_pop(Queue *q){
    pthread_mutex_lock(&q->mtx);
    while (!q->head) pthread_cond_wait(&q->cv, &q->mtx);
    QNode *n = q->head; q->head = n->next; if (!q->head) q->tail = NULL;
    pthread_mutex_unlock(&q->mtx);
    void *it = n->item; free(n); return it;
}

typedef struct ActiveObject ActiveObject;
typedef void (*AOHandler)(ActiveObject*, void*);

struct ActiveObject {
    const char *name;
    Queue q;
    pthread_t tid;
    AOHandler handle;
};

static void* ao_thread_main(void *arg){
    ActiveObject *ao = (ActiveObject*)arg;
    for (;;) {
        void *job = q_pop(&ao->q);
        ao->handle(ao, job);
    }
    return NULL;
}

static void ao_start(ActiveObject *ao, const char *name, AOHandler h){
    ao->name = name; ao->handle = h; q_init(&ao->q);
    if (pthread_create(&ao->tid, NULL, ao_thread_main, ao) != 0) {
        perror("pthread_create"); exit(1);
    }
    pthread_detach(ao->tid);
}


typedef enum {
    CMD_EULER, CMD_MST, CMD_MAXCLIQUE, CMD_COUNTCLQ3P, CMD_HAMILTON
} AlgoCmd;

typedef struct {
    int cfd;                
    AlgoCmd cmd;            
    Graph *g;               
    char  *prefix;          
} Request;

typedef struct {
    int cfd;                
    char *text;             
} SendTask;

static ActiveObject AO_SENDER;

static void handle_send(ActiveObject *ao, void *item){
    (void)ao;
    SendTask *s = (SendTask*)item;
    if (s->text) (void)write_all(s->cfd, s->text, strlen(s->text));
    close(s->cfd);
    free(s->text);
    free(s);
}

static void emit_and_send(Request *R, const char *body){
    StrBuf out; sb_init(&out);
    if (R->prefix) sb_append(&out, R->prefix, strlen(R->prefix));
    sb_append(&out, body, strlen(body));

    SendTask *S = (SendTask*)malloc(sizeof(SendTask));
    if (!S) { perror("malloc"); exit(1); }
    S->cfd = R->cfd;
    S->text = out.buf; 

    q_push(&AO_SENDER.q, S);

    free(R->prefix);
    free_graph(R->g);
    free(R);
}

static ActiveObject AO_EULER, AO_MST, AO_MAXCLQ, AO_CNTCLQ3P, AO_HAM;

static void handle_euler(ActiveObject *ao, void *item){
    (void)ao;
    Request *R = (Request*)item;
    StrBuf b; sb_init(&b);

    if (!connected_among_non_isolated(R->g)) {
        sb_printf(&b, "No Euler circuit: graph is disconnected among non-isolated vertices.\n");
        emit_and_send(R, b.buf ? b.buf : "");
        sb_free(&b); return;
    }
    int odd = 0; for (int i=0;i<R->g->V;++i) if (degree(R->g,i)%2) odd++;
    if (odd) {
        sb_printf(&b, "No Euler circuit: %d vertices have odd degree.\n", odd);
        emit_and_send(R, b.buf ? b.buf : "");
        sb_free(&b); return;
    }
    int *path=NULL,len=0;
    if (!euler_circuit(R->g, &path, &len)) {
        sb_printf(&b, "No Euler circuit (unexpected after checks).\n");
        emit_and_send(R, b.buf ? b.buf : "");
        sb_free(&b); return;
    }
    sb_printf(&b, "Euler circuit exists. Sequence of vertices:\n");
    for (int i=0;i<len;++i) sb_printf(&b, "%d%s", path[i], (i+1==len) ? "\n" : " -> ");
    free(path);
    emit_and_send(R, b.buf ? b.buf : "");
    sb_free(&b);
}

static void handle_mst(ActiveObject *ao, void *item){
    (void)ao;
    Request *R = (Request*)item;
    StrBuf b; sb_init(&b);
    long long w = mst_weight_prim(R->g);
    if (w < 0) sb_printf(&b, "MST: graph is not connected (no spanning tree)\n");
    else       sb_printf(&b, "MST total weight: %lld\n", w);
    emit_and_send(R, b.buf ? b.buf : "");
    sb_free(&b);
}

static void handle_maxclq(ActiveObject *ao, void *item){
    (void)ao;
    Request *R = (Request*)item;
    StrBuf b; sb_init(&b);
    int *cl = (int*)malloc((size_t)R->g->V * sizeof(int));
    int got = 0, k = max_clique(R->g, cl, &got);
    sb_printf(&b, "Max clique size = %d\n", k);
    if (got > 0) {
        sb_printf(&b, "Vertices: ");
        for (int i=0;i<got;++i) sb_printf(&b, "%d%s", cl[i], (i+1==got)?"\n":" ");
    }
    free(cl);
    emit_and_send(R, b.buf ? b.buf : "");
    sb_free(&b);
}

static void handle_cntclq3p(ActiveObject *ao, void *item){
    (void)ao;
    Request *R = (Request*)item;
    StrBuf b; sb_init(&b);
    long long cnt = count_cliques_3plus(R->g);
    sb_printf(&b, "Number of cliques (size >= 3): %lld\n", cnt);
    emit_and_send(R, b.buf ? b.buf : "");
    sb_free(&b);
}

static void handle_ham(ActiveObject *ao, void *item){
    (void)ao;
    Request *R = (Request*)item;
    StrBuf b; sb_init(&b);
    int *cyc=NULL, L=0;
    if (!hamilton_cycle(R->g, &cyc, &L)) {
        sb_printf(&b, "No Hamiltonian cycle.\n");
        emit_and_send(R, b.buf ? b.buf : "");
        sb_free(&b); return;
    }
    sb_printf(&b, "Hamiltonian cycle found:\n");
    for (int i=0;i<L;++i) sb_printf(&b, "%d%s", cyc[i], (i+1==L)?"\n":" -> ");
    free(cyc);
    emit_and_send(R, b.buf ? b.buf : "");
    sb_free(&b);
}

static char* make_prefix_if_p(const Graph *g, bool want_print){
    if (!want_print) return NULL;
    StrBuf b; sb_init(&b);
    sb_printf(&b, "Graph: V=%d, E=%d\nAdjacency matrix:\n", g->V, g->E);
    for (int i=0;i<g->V;++i){
        for (int j=0;j<g->V;++j) sb_printf(&b, "%d ", g->adj[i][j]);
        sb_printf(&b, "\n");
    }
    return b.buf; 
}


static int g_listen_fd = -1;
static pthread_mutex_t lf_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  lf_cv  = PTHREAD_COND_INITIALIZER;
static int has_leader = 0;

static pthread_mutex_t rng_mtx = PTHREAD_MUTEX_INITIALIZER;

static void route_to_ao(Request *R){
    switch (R->cmd) {
        case CMD_EULER:      q_push(&AO_EULER.q, R); break;
        case CMD_MST:        q_push(&AO_MST.q, R); break;
        case CMD_MAXCLIQUE:  q_push(&AO_MAXCLQ.q, R); break;
        case CMD_COUNTCLQ3P: q_push(&AO_CNTCLQ3P.q, R); break;
        case CMD_HAMILTON:   q_push(&AO_HAM.q, R); break;
        default: 
            close(R->cfd); free_graph(R->g); free(R->prefix); free(R);
    }
}

static ssize_t read_line_req(int fd, char *buf, size_t cap) { return read_line(fd, buf, cap); }

static void handle_client_header_and_dispatch(int cfd) {
    char line[MAX_LINE];
    if (read_line_req(cfd, line, sizeof(line)) <= 0) { close(cfd); return; }

    char *tok[8]; int ntok=0;
    for (char *p=strtok(line," \t\r\n"); p && ntok<8; p=strtok(NULL," \t\r\n")) tok[ntok++]=p;

    if (ntok < 4) {
        sendf_fd(cfd, "ERR usage:\n"
                      "  <ALGO> <E> <V> <SEED> [-p]\n"
                      "  <ALGO> GRAPH <E> <V> [-p]  (then E lines: u v [w])\n");
        close(cfd); return;
    }

    AlgoCmd cmd;
    if      (strcmp(tok[0],"EULER")==0)      cmd = CMD_EULER;
    else if (strcmp(tok[0],"MST")==0)        cmd = CMD_MST;
    else if (strcmp(tok[0],"MAXCLIQUE")==0)  cmd = CMD_MAXCLIQUE;
    else if (strcmp(tok[0],"COUNTCLQ3P")==0) cmd = CMD_COUNTCLQ3P;
    else if (strcmp(tok[0],"HAMILTON")==0)   cmd = CMD_HAMILTON;
    else {
        sendf_fd(cfd, "ERR unknown ALGO. Supported: EULER MST MAXCLIQUE COUNTCLQ3P HAMILTON\n");
        close(cfd); return;
    }

    bool want_print = false;
    int E=-1, V=-1; unsigned int seed=0;
    Graph *g = NULL;

    if (strcmp(tok[1], "GRAPH") == 0) {
        if (ntok < 4 || ntok > 5) { sendf_fd(cfd, "ERR usage: <ALGO> GRAPH <E> <V> [-p]\n"); close(cfd); return; }
        if (!parse_int(tok[2], &E) || !parse_int(tok[3], &V)) { sendf_fd(cfd, "ERR bad <E> or <V>\n"); close(cfd); return; }
        if (ntok == 5) {
            if (strcmp(tok[4], "-p") != 0) { sendf_fd(cfd, "ERR bad flag. Use -p or omit.\n"); close(cfd); return; }
            want_print = true;
        }
        if (V < 1 || E < 0) { sendf_fd(cfd, "ERR invalid: V >= 1, E >= 0\n"); close(cfd); return; }
        long long maxE = (long long)V * (V - 1) / 2;
        if ((long long)E > maxE) { sendf_fd(cfd, "ERR invalid: E <= V*(V-1)/2 (max=%lld)\n", maxE); close(cfd); return; }

        g = create_graph(V);
        for (int i=0;i<E;++i){
            char el[256];
            if (read_line_req(cfd, el, sizeof(el)) <= 0) {
                sendf_fd(cfd, "ERR expected %d edge lines; got %d\n", E, i);
                free_graph(g); close(cfd); return;
            }
            char *a=strtok(el," \t\r\n"), *b=strtok(NULL," \t\r\n"), *c=strtok(NULL," \t\r\n");
            if (!a||!b){ sendf_fd(cfd,"ERR edge line format: u v [w]\n"); free_graph(g); close(cfd); return; }
            int u,v,w=1;
            if (!parse_int(a,&u) || !parse_int(b,&v)){ sendf_fd(cfd,"ERR edge endpoints\n"); free_graph(g); close(cfd); return; }
            if (c){ int tw; if(!parse_int(c,&tw)||tw<=0){ sendf_fd(cfd,"ERR weight must be positive\n"); free_graph(g); close(cfd); return; } w=tw; }
            if (u<0||u>=V||v<0||v>=V||u==v){ sendf_fd(cfd,"ERR invalid edge %d: (%d,%d)\n",i,u,v); free_graph(g); close(cfd); return; }
            (void)graph_add_edge(g, u, v, w); // ignore duplicates
        }
    } else {
        if (ntok < 4 || ntok > 5) { sendf_fd(cfd, "ERR usage: <ALGO> <E> <V> <SEED> [-p]\n"); close(cfd); return; }
        if (!parse_int(tok[1], &E) || !parse_int(tok[2], &V) || !parse_uint(tok[3], &seed)) {
            sendf_fd(cfd, "ERR bad params.\n"); close(cfd); return;
        }
        if (ntok == 5) {
            if (strcmp(tok[4], "-p") != 0) { sendf_fd(cfd, "ERR bad flag. Use -p or omit.\n"); close(cfd); return; }
            want_print = true;
        }
        if (V < 1 || E < 0) { sendf_fd(cfd, "ERR invalid: V >= 1, E >= 0\n"); close(cfd); return; }
        long long maxE = (long long)V * (V - 1) / 2;
        if ((long long)E > maxE) { sendf_fd(cfd, "ERR invalid: E <= V*(V-1)/2 (max=%lld)\n", maxE); close(cfd); return; }

        g = create_graph(V);
        pthread_mutex_lock(&rng_mtx);
        generate_random_graph(g, E, seed);
        pthread_mutex_unlock(&rng_mtx);
    }

    char *prefix = make_prefix_if_p(g, want_print);

    Request *R = (Request*)malloc(sizeof(Request));
    if (!R) { perror("malloc"); free_graph(g); free(prefix); close(cfd); return; }
    R->cfd = cfd; R->cmd = cmd; R->g = g; R->prefix = prefix;

    route_to_ao(R);
}

static void *worker_main(void *arg){
    (void)arg;
    for (;;) {
        pthread_mutex_lock(& (lf_mtx) );
        while (has_leader) pthread_cond_wait(&lf_cv, &lf_mtx);
        has_leader = 1;
        pthread_mutex_unlock(&lf_mtx);

        int cfd = accept(g_listen_fd, NULL, NULL);
        int err = (cfd < 0) ? errno : 0;

        pthread_mutex_lock(&lf_mtx);
        has_leader = 0;
        pthread_cond_signal(&lf_cv);
        pthread_mutex_unlock(&lf_mtx);

        if (cfd < 0) { if (err == EINTR) continue; continue; }

        handle_client_header_and_dispatch(cfd);
    }
    return NULL;
}


int main(int argc, char **argv){
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <port> [threads]\n", argv[0]); return 2;
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) { fprintf(stderr, "Invalid port\n"); return 2; }

    int nthreads = 0;
    if (argc == 3) nthreads = atoi(argv[2]);
    else {
        long n = sysconf(_SC_NPROCESSORS_ONLN);
        nthreads = (n > 0) ? (int)n : 4;
    }
    if (nthreads < 1) nthreads = 1;

    ao_start(&AO_SENDER,   "SENDER",      handle_send);
    ao_start(&AO_EULER,    "EULER_AO",    handle_euler);
    ao_start(&AO_MST,      "MST_AO",      handle_mst);
    ao_start(&AO_MAXCLQ,   "MAXCLIQUE_AO",handle_maxclq);
    ao_start(&AO_CNTCLQ3P, "COUNTCLQ3P_AO",handle_cntclq3p);
    ao_start(&AO_HAM,      "HAMILTON_AO", handle_ham);

    g_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) { perror("socket"); return 1; }
    int yes = 1; setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(g_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(g_listen_fd, BACKLOG) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "server listening on port %d with %d acceptor threads\n", port, nthreads);

    for (int i=0;i<nthreads;++i){
        pthread_t tid;
        if (pthread_create(&tid, NULL, worker_main, NULL) != 0) { perror("pthread_create"); return 1; }
        pthread_detach(tid);
    }

    for (;;) pause();
    return 0;
}