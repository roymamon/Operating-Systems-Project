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