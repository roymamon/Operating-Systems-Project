#pragma once
#include <stddef.h>

typedef struct {
    int V;      // number of vertices
    int E;      // number of edges
    int **adj;  // adjacency matrix (0/1), undirected
    int **w;    // symmetric positive weights where adj[u][v]==1
} Graph;

/* Construction / teardown */
Graph* create_graph(int V);
void   free_graph(Graph *g);

/* Random generator (assigns random positive weights to added edges) */
void   generate_random_graph(Graph *g, int targetE, unsigned int seed);

/* Add a single undirected edge (u,v) with positive weight w (default 1 if <=0).
   Returns 1 if added, 0 if invalid/duplicate/self-loop/out-of-range. */
int    graph_add_edge(Graph *g, int u, int v, int w);

/* Helpers and checks */
int    degree(const Graph *g, int u);
int    connected_among_non_isolated(const Graph *g);
int    all_even_degrees(const Graph *g);

/* Optional debug */
void   print_graph(const Graph *g);

/* Euler circuit (Hierholzer). Returns 1 on success and fills (path,path_len). */
int    euler_circuit(const Graph *g, int **path_out, int *path_len_out);

/* MST (Prim, O(V^2)). Returns total weight, or -1 if disconnected. */
long long mst_weight_prim(const Graph *g);

/* Max Clique (Bron–Kerbosch with pivot). */
int    max_clique(const Graph *g, int *clique_out, int *clique_size_out);

/* Count all cliques of size >= 3. */
long long count_cliques_3plus(const Graph *g);

/* Hamiltonian cycle: returns 1 and fills (cycle, len=V+1) if found; else 0. */
int    hamilton_cycle(const Graph *g, int **cycle_out, int *cycle_len_out);