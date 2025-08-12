// Standalone example run (CLI build only): ./graph <edges> <vertices> [seed] [-p]

#define _XOPEN_SOURCE 700
#include "graph.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <math.h>   // not strictly needed
#include <stdint.h>
/* Tunable for random weights */
#ifndef GRAPH_RAND_WMAX
#define GRAPH_RAND_WMAX 100   // edges get weights in [1..GRAPH_RAND_WMAX]
#endif

/*------------------ Graph utils ------------------*/

Graph* create_graph(int V) {
    Graph *g = malloc(sizeof(Graph));
    if (!g) { perror("malloc"); exit(1); }
    g->V = V;
    g->E = 0;

    g->adj = malloc(V * sizeof(int*));
    if (!g->adj) { perror("malloc"); exit(1); }
    g->w   = malloc(V * sizeof(int*));
    if (!g->w)   { perror("malloc"); exit(1); }

    for (int i = 0; i < V; ++i) {
        g->adj[i] = calloc(V, sizeof(int));
        if (!g->adj[i]) { perror("calloc"); exit(1); }
        g->w[i] = calloc(V, sizeof(int));
        if (!g->w[i]) { perror("calloc"); exit(1); }
    }
    return g;
}

void free_graph(Graph *g) {
    if (!g) return;
    for (int i = 0; i < g->V; ++i) {
        free(g->adj[i]);
        free(g->w[i]);
    }
    free(g->adj);
    free(g->w);
    free(g);
}

/* internal: add edge with explicit weight (>0) */
static int add_edge_w(Graph *g, int u, int v, int w) {
    if (u < 0 || v < 0 || u >= g->V || v >= g->V) return 0;
    if (u == v) return 0;              // no self-loops
    if (w <= 0) return 0;              // positive weights only
    if (g->adj[u][v]) return 0;        // no multiedges

    g->adj[u][v] = g->adj[v][u] = 1;
    g->w[u][v]   = g->w[v][u]   = w;
    g->E++;
    return 1;
}

int degree(const Graph *g, int u) {
    int d = 0;
    for (int v = 0; v < g->V; ++v) d += g->adj[u][v];
    return d;
}

void print_graph(const Graph *g) {
    printf("Graph: V=%d, E=%d\nAdjacency matrix:\n", g->V, g->E);
    for (int i = 0; i < g->V; ++i) {
        for (int j = 0; j < g->V; ++j) {
            printf("%d ", g->adj[i][j]);
        }
        printf("\n");
    }
    //If you want, also print weights (commented to keep server output stable)
    printf("Weights matrix:\n");
    for (int i = 0; i < g->V; ++i) {
        for (int j = 0; j < g->V; ++j) {
            printf("%d ", g->w[i][j]);
        }
        printf("\n");
    }
    
}

/*------------------ Random graph generator ------------------*/

void generate_random_graph(Graph *g, int targetE, unsigned int seed) {
    srand(seed);
    const long long maxE = (long long)g->V * (g->V - 1) / 2;
    if (targetE > maxE) {
        fprintf(stderr, "Error: cannot place %d edges in a simple graph with V=%d (max=%lld)\n",
                targetE, g->V, maxE);
        exit(1);
    }
    while (g->E < targetE) {
        int u = rand() % g->V;
        int v = rand() % g->V;
        int w = (rand() % GRAPH_RAND_WMAX) + 1;   // weight in [1..WMAX]
        (void)add_edge_w(g, u, v, w);
    }
}

/*------------------ Connectivity ------------------*/

static void dfs(const Graph *g, int u, int *visited) {
    visited[u] = 1;
    for (int v = 0; v < g->V; ++v) {
        if (g->adj[u][v] && !visited[v]) dfs(g, v, visited);
    }
}

int connected_among_non_isolated(const Graph *g) {
    int start = -1;
    for (int i = 0; i < g->V; ++i) if (degree(g, i) > 0) { start = i; break; }
    if (start == -1) return 1; // no edges: treat as Eulerian-trivial
    int *visited = calloc(g->V, sizeof(int));
    if (!visited) { perror("calloc"); exit(1); }
    dfs(g, start, visited);
    int ok = 1;
    for (int i = 0; i < g->V; ++i) {
        if (degree(g, i) > 0 && !visited[i]) { ok = 0; break; }
    }
    free(visited);
    return ok;
}

/*------------------ Euler circuit ------------------*/

int all_even_degrees(const Graph *g) {
    for (int i = 0; i < g->V; ++i)
        if (degree(g, i) % 2 != 0) return 0;
    return 1;
}

int euler_circuit(const Graph *g, int **path_out, int *path_len_out) {
    if (!connected_among_non_isolated(g)) return 0;
    if (!all_even_degrees(g)) return 0;

    int **adj = malloc(g->V * sizeof(int*));
    if (!adj) { perror("malloc"); exit(1); }
    for (int i = 0; i < g->V; ++i) {
        adj[i] = malloc(g->V * sizeof(int));
        if (!adj[i]) { perror("malloc"); exit(1); }
        memcpy(adj[i], g->adj[i], g->V * sizeof(int));
    }
    int *deg = malloc(g->V * sizeof(int));
    if (!deg) { perror("malloc"); exit(1); }
    for (int i = 0; i < g->V; ++i) deg[i] = degree(g, i);

    int start = 0;
    for (int i = 0; i < g->V; ++i) if (deg[i] > 0) { start = i; break; }

    int stackCap = g->E + 2, top = 0;
    int *stack = malloc(stackCap * sizeof(int));
    if (!stack) { perror("malloc"); exit(1); }

    int outCap = g->E + 2, outLen = 0;
    int *out = malloc(outCap * sizeof(int));
    if (!out) { perror("malloc"); exit(1); }

    stack[top++] = start;
    while (top > 0) {
        int u = stack[top - 1], v = -1;
        if (deg[u] > 0) {
            for (int w = 0; w < g->V; ++w) if (adj[u][w]) { v = w; break; }
        }
        if (v != -1) {
            adj[u][v]--; adj[v][u]--;
            deg[u]--; deg[v]--;
            if (top >= stackCap) {
                stackCap *= 2;
                stack = realloc(stack, stackCap * sizeof(int));
                if (!stack) { perror("realloc"); exit(1); }
            }
            stack[top++] = v;
        } else {
            if (outLen >= outCap) {
                outCap *= 2;
                out = realloc(out, outCap * sizeof(int));
                if (!out) { perror("realloc"); exit(1); }
            }
            out[outLen++] = u;
            top--;
        }
    }

    for (int i = 0; i < g->V; ++i) free(adj[i]);
    free(adj);
    free(deg);
    free(stack);

    *path_out = out;
    *path_len_out = outLen;
    return 1;
}

/*------------------ MST (Prim, O(V^2)) ------------------*/

long long mst_weight_prim(const Graph *g) {
    const int V = g->V;
    if (V == 0) return 0;
    if (V == 1) return 0;

    // If any vertex is isolated or graph disconnected, no spanning tree
    for (int i = 0; i < V; ++i) {
        if (degree(g, i) == 0) return -1;  // isolated vertex => cannot span all V
    }
    // Quick connectivity check (stronger than "among non-isolated")
    // Do a DFS from 0 and require all vertices to be visited.
    int *vis = calloc(V, sizeof(int));
    if (!vis) { perror("calloc"); exit(1); }
    // reuse local DFS
    // inline DFS:
    int stackSize = V, top = 0;
    int *stack = malloc(stackSize * sizeof(int));
    if (!stack) { perror("malloc"); exit(1); }
    stack[top++] = 0; vis[0] = 1;
    while (top) {
        int u = stack[--top];
        for (int v = 0; v < V; ++v) {
            if (g->adj[u][v] && !vis[v]) {
                vis[v] = 1;
                if (top >= stackSize) {
                    stackSize *= 2;
                    stack = realloc(stack, stackSize * sizeof(int));
                    if (!stack) { perror("realloc"); exit(1); }
                }
                stack[top++] = v;
            }
        }
    }
    for (int i = 0; i < V; ++i) {
        if (!vis[i]) { free(vis); free(stack); return -1; }
    }
    free(vis);
    free(stack);

    // Prim
    const int INF = INT_MAX / 4;
    int *key    = malloc(V * sizeof(int));
    int *inMST  = calloc(V, sizeof(int));
    if (!key || !inMST) { perror("malloc"); exit(1); }

    for (int i = 0; i < V; ++i) key[i] = INF;
    key[0] = 0;

    long long total = 0;

    for (int it = 0; it < V; ++it) {
        int u = -1, best = INF;
        for (int i = 0; i < V; ++i) {
            if (!inMST[i] && key[i] < best) {
                best = key[i]; u = i;
            }
        }
        if (u == -1 || best == INF) {  // disconnected (shouldn’t happen after check)
            free(key); free(inMST);
            return -1;
        }
        inMST[u] = 1;
        total += (it == 0 ? 0 : best);

        // relax neighbors
        for (int v = 0; v < V; ++v) {
            if (!inMST[v] && g->adj[u][v]) {
                int w = g->w[u][v];
                if (w < key[v]) key[v] = w;
            }
        }
    }

    free(key);
    free(inMST);
    return total;
}

/*======================  Maximum Clique  ======================*/
/* Bron–Kerbosch with pivot, implemented using dynamic bitsets. */

/* ---- tiny dynamic bitset ---- */
typedef struct {
    int nbits;
    int nwords;          // = (nbits + 63) / 64
    uint64_t *w;         // length nwords
} Bitset;

static Bitset bs_make(int nbits) {
    Bitset b;
    b.nbits = nbits;
    b.nwords = (nbits + 63) / 64;
    b.w = calloc((size_t)b.nwords, sizeof(uint64_t));
    if (!b.w) { perror("calloc"); exit(1); }
    return b;
}
static void bs_free(Bitset *b) { free(b->w); b->w = NULL; }
static inline void bs_zero(Bitset *b){ memset(b->w, 0, b->nwords*sizeof(uint64_t)); }
static inline void bs_set(Bitset *b, int i){ b->w[i>>6] |= (UINT64_C(1)<<(i&63)); }
static inline int  bs_test(const Bitset *b, int i){ return (int)((b->w[i>>6]>>(i&63))&1U); }
static inline void bs_copy(Bitset *dst, const Bitset *src){
    memcpy(dst->w, src->w, (size_t)dst->nwords * sizeof(uint64_t));
}
static int bs_empty(const Bitset *b) {
    for (int k = 0; k < b->nwords; k++) {
        if (b->w[k])
            return 0;
    }
    return 1;
}
static inline int  bs_count(const Bitset *b){
    int s=0; for(int k=0;k<b->nwords;k++) s += __builtin_popcountll(b->w[k]); return s;
}
static inline void bs_or(Bitset *a, const Bitset *b){
    for (int k=0;k<a->nwords;k++) a->w[k] |= b->w[k];
}
static inline void bs_and(Bitset *a, const Bitset *b){
    for (int k=0;k<a->nwords;k++) a->w[k] &= b->w[k];
}
static inline void bs_minus(Bitset *a, const Bitset *b){
    for (int k=0;k<a->nwords;k++) a->w[k] &= ~b->w[k];
}
static inline void bs_clear(Bitset *b, int i){
    b->w[i>>6] &= ~(UINT64_C(1)<<(i&63));
}

/* Precompute neighbor masks N[v] as bitsets for quick intersections. */
typedef struct {
    int V;
    Bitset *N;          // array length V, each a bitset of neighbors of v
} NBMasks;

static NBMasks nb_build(const Graph *g){
    NBMasks nb; nb.V = g->V;
    nb.N = malloc((size_t)g->V * sizeof(Bitset));
    if (!nb.N) { perror("malloc"); exit(1); }
    for (int v=0; v<g->V; ++v){
        nb.N[v] = bs_make(g->V);
        for (int u=0; u<g->V; ++u) if (g->adj[v][u]) bs_set(&nb.N[v], u);
    }
    return nb;
}
static void nb_free(NBMasks *nb){
    if (!nb->N) return;
    for (int v=0; v<nb->V; ++v) bs_free(&nb->N[v]);
    free(nb->N); nb->N=NULL;
}

/* Choose a pivot u from P∪X that maximizes |P ∩ N(u)| (Tomita pivot) */
static int choose_pivot(const Bitset *P, const Bitset *X, const NBMasks *nb){
    // U = P ∪ X
    Bitset U = bs_make(P->nbits);
    bs_copy(&U, P); bs_or(&U, X);

    int best_u = -1, best_deg = -1;
    for (int word=0; word<U.nwords; ++word){
        uint64_t w = U.w[word];
        while (w){
            int bit = __builtin_ctzll(w);
            int u = (word<<6) + bit;
            if (u >= U.nbits) break;
            // deg = |P ∩ N(u)|
            Bitset tmp = bs_make(P->nbits);
            bs_copy(&tmp, P);
            bs_and(&tmp, &nb->N[u]);
            int deg = bs_count(&tmp);
            bs_free(&tmp);
            if (deg > best_deg){ best_deg = deg; best_u = u; }
            w &= (w-1);
        }
    }
    bs_free(&U);
    return best_u;     // may be -1 if P and X empty (base case)
}

/* Global best (kept local to the call via a small struct). */
typedef struct {
    int best_size;
    Bitset best_R;
    const NBMasks *nb;
} BKState;

static void BK_recurse(Bitset *R, Bitset *P, Bitset *X, BKState *S){
    if (bs_empty(P) && bs_empty(X)){
        int sz = bs_count(R);
        if (sz > S->best_size){
            S->best_size = sz;
            bs_copy(&S->best_R, R);
        }
        return;
    }

    int u = choose_pivot(P, X, S->nb);        // pivot
    Bitset P_without_Nu = bs_make(P->nbits);
    bs_copy(&P_without_Nu, P);
    if (u >= 0) bs_minus(&P_without_Nu, &S->nb->N[u]);

    // Iterate vertices v in P \ N(u)
    for (int word=0; word<P_without_Nu.nwords; ++word){
        uint64_t w = P_without_Nu.w[word];
        while (w){
            int bit = __builtin_ctzll(w);
            int v = (word<<6) + bit;
            if (v >= P_without_Nu.nbits) break;

            // R' = R ∪ {v}
            Bitset Rp = bs_make(R->nbits); bs_copy(&Rp, R); bs_set(&Rp, v);

            // P' = P ∩ N(v), X' = X ∩ N(v)
            Bitset Pp = bs_make(P->nbits); bs_copy(&Pp, P); bs_and(&Pp, &S->nb->N[v]);
            Bitset Xp = bs_make(X->nbits); bs_copy(&Xp, X); bs_and(&Xp, &S->nb->N[v]);

            // Branch
            BK_recurse(&Rp, &Pp, &Xp, S);

            // P = P \ {v}; X = X ∪ {v}
            Bitset singleton = bs_make(P->nbits); bs_set(&singleton, v);
            bs_minus(P, &singleton);
            bs_or(X, &singleton);
            bs_free(&singleton);

            bs_free(&Rp); bs_free(&Pp); bs_free(&Xp);

            w &= (w-1); // next set bit in P_without_Nu word
        }
    }
    bs_free(&P_without_Nu);
}

int max_clique(const Graph *g, int *clique_out, int *clique_size_out){
    const int V = g->V;
    NBMasks nb = nb_build(g);

    Bitset R = bs_make(V), P = bs_make(V), X = bs_make(V);
    for (int v=0; v<V; ++v) bs_set(&P, v);

    BKState S;
    S.best_size = 0;
    S.best_R = bs_make(V);
    S.nb = &nb;

    BK_recurse(&R, &P, &X, &S);

    // Marshal result
    if (clique_out){
        int k = 0;
        for (int word=0; word<S.best_R.nwords; ++word){
            uint64_t w = S.best_R.w[word];
            while (w){
                int bit = __builtin_ctzll(w);
                int v = (word<<6) + bit;
                if (v < V) clique_out[k++] = v;
                w &= (w-1);
            }
        }
        if (clique_size_out) *clique_size_out = S.best_size;
    } else if (clique_size_out){
        *clique_size_out = S.best_size;
    }

    // cleanup
    bs_free(&R); bs_free(&P); bs_free(&X);
    bs_free(&S.best_R);
    nb_free(&nb);

    return S.best_size;
}
/*================ Count all cliques of size >= 3 ================*/
/* Enumerate ALL cliques (not only maximal). No pivot pruning here,
   because pivot pruning enumerates maximal cliques only. */

static void BK_count_all(Bitset *R, int sizeR, Bitset *P,
                         const NBMasks *nb, long long *cnt)
{
    if (sizeR >= 3) (*cnt)++;  // count this clique

    // We’ll iterate over a snapshot of P, while mutating P as we go
    Bitset Pc = bs_make(P->nbits);
    bs_copy(&Pc, P);

    for (int word = 0; word < Pc.nwords; ++word) {
        uint64_t mask = Pc.w[word];
        while (mask) {
            int bit = __builtin_ctzll(mask);
            int v = (word << 6) + bit;
            if (v >= Pc.nbits) break;
            mask &= (mask - 1);

            // Remove v from P
            bs_clear(P, v);

            // Rp = R ∪ {v}
            Bitset Rp = bs_make(R->nbits);
            bs_copy(&Rp, R);
            bs_set(&Rp, v);

            // P' = P ∩ N(v)
            Bitset Pp = bs_make(P->nbits);
            bs_copy(&Pp, P);
            bs_and(&Pp, &nb->N[v]);

            // Recurse
            BK_count_all(&Rp, sizeR + 1, &Pp, nb, cnt);

            bs_free(&Rp);
            bs_free(&Pp);
        }
    }
    bs_free(&Pc);
}

long long count_cliques_3plus(const Graph *g)
{
    const int V = g->V;
    if (V <= 2) return 0;

    NBMasks nb = nb_build(g);

    Bitset R = bs_make(V);
    Bitset P = bs_make(V);
    for (int v = 0; v < V; ++v) bs_set(&P, v);

    long long cnt = 0;
    BK_count_all(&R, 0, &P, &nb, &cnt);

    bs_free(&R);
    bs_free(&P);
    nb_free(&nb);
    return cnt;
}

/*======================  Hamiltonian Cycle (backtracking)  ======================*/

static int ham_backtrack(const Graph *g, int start, int pos, int *path, unsigned char *used) {
    if (pos == g->V) {
        int last = path[g->V - 1];
        return g->adj[last][start] ? 1 : 0;  // close the cycle
    }

    int prev = path[pos - 1];
    // Try neighbors of prev
    for (int v = 0; v < g->V; ++v) {
        if (!g->adj[prev][v]) continue;      // must be adjacent
        if (used[v]) continue;               // must be unused
        // small pruning: avoid dead ends early
        if (degree(g, v) < 2) continue;

        used[v] = 1;
        path[pos] = v;
        if (ham_backtrack(g, start, pos + 1, path, used)) return 1;
        used[v] = 0;
    }
    return 0;
}

int hamilton_cycle(const Graph *g, int **cycle_out, int *cycle_len_out) {
    if (!g || g->V < 3) return 0;

    // Basic feasibility checks
    if (!connected_among_non_isolated(g)) return 0;
    for (int i = 0; i < g->V; ++i) {
        if (degree(g, i) < 2) return 0;  // every vertex in a cycle needs degree >= 2
    }

    int *path = (int *)malloc((size_t)g->V * sizeof(int));
    if (!path) { perror("malloc"); exit(1); }
    unsigned char *used = (unsigned char *)calloc((size_t)g->V, 1);
    if (!used) { perror("calloc"); exit(1); }

    int start = 0;                 // fix an arbitrary start (eliminates cyclic symmetries)
    path[0] = start;
    used[start] = 1;

    int found = ham_backtrack(g, start, 1, path, used);

    free(used);

    if (!found) { free(path); return 0; }

    // Marshal output as V+1 vertices to close the cycle
    int *cycle = (int *)malloc((size_t)(g->V + 1) * sizeof(int));
    if (!cycle) { perror("malloc"); exit(1); }
    for (int i = 0; i < g->V; ++i) cycle[i] = path[i];
    cycle[g->V] = path[0];

    free(path);

    if (cycle_out) *cycle_out = cycle; else free(cycle);
    if (cycle_len_out) *cycle_len_out = g->V + 1;
    return 1;
}

/*------------------ CLI main (compiled out for server) ------------------*/
#ifndef GRAPH_NO_MAIN
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <edges> <vertices> [seed] [-p]\n", argv[0]);
        return 1;
    }

    int E = atoi(argv[1]);
    int V = atoi(argv[2]);
    unsigned int seed = (argc >= 4 && argv[3][0] != '-') ?
        (unsigned int)strtoul(argv[3], NULL, 10) :
        (unsigned int)time(NULL);
    int printAdj = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0) printAdj = 1;
    }

    if (V < 1 || E < 0) {
        fprintf(stderr, "Invalid vertices or edges\n");
        return 1;
    }

    Graph *g = create_graph(V);
    generate_random_graph(g, E, seed);

    if (printAdj) print_graph(g);

    // Example: show MST too (optional)
    long long mst = mst_weight_prim(g);
    if (mst >= 0) {
        printf("MST total weight: %lld\n", mst);
    } else {
        printf("MST: graph is not connected (no spanning tree)\n");
    }

    //example max clique
    int *cl = malloc(g->V * sizeof(int));
    int cs = 0;
    int sz = max_clique(g, cl, &cs);
    printf("Max clique size = %d\n", sz);
    printf("Vertices: ");
    for (int i=0;i<cs;i++) printf("%d%s", cl[i], (i+1==cs)?"\n":" ");
    free(cl);
    
    //number of cliques
    long long c3 = count_cliques_3plus(g);
    printf("Number of cliques (sized >= 3): %lld\n", c3);

    //hamilton cycle
    int *hc = NULL, hlen = 0;
    if (hamilton_cycle(g, &hc, &hlen)) {
    printf("Hamiltonian cycle found: ");
    for (int i = 0; i < hlen; ++i) {
        printf("%d%s", hc[i], (i + 1 == hlen) ? "\n" : " -> ");
    }
    free(hc);
    } else {
      printf("No Hamiltonian cycle.\n");
    }
    

    // Keep your Euler demo
    if (!connected_among_non_isolated(g)) {
        printf("No Euler circuit: graph is disconnected among non-isolated vertices.\n");
        free_graph(g);
        return 0;
    }

    int oddCount = 0;
    for (int i = 0; i < g->V; ++i) if (degree(g, i) % 2 != 0) oddCount++;
    if (oddCount != 0) {
        printf("No Euler circuit: %d vertices have odd degree.\n", oddCount);
        free_graph(g);
        return 0;
    }

    int *path = NULL, pathLen = 0;
    euler_circuit(g, &path, &pathLen);

    printf("Euler circuit exists. Sequence of vertices:\n");
    for (int i = 0; i < pathLen; ++i) {
        printf("%d%s", path[i], (i + 1 == pathLen ? "\n" : " -> "));
    }

    
    free(path);
    free_graph(g);
    return 0;
}
#endif