#pragma once
#include <stddef.h>

typedef struct {
    int V;      // number of vertices
    int E;      // number of edges
    int **adj;  // adjacency matrix (0/1), undirected
    int **w;    // weight matrix; valid only where adj[u][v]==1 (symmetric, positive)
} Graph;

/* Construction / teardown */
Graph* create_graph(int V);
void   free_graph(Graph *g);

/* Random generator (assigns random positive weights to added edges) */
void   generate_random_graph(Graph *g, int targetE, unsigned int seed);

/* Helpers and checks */
int    degree(const Graph *g, int u);
int    connected_among_non_isolated(const Graph *g);
int    all_even_degrees(const Graph *g);

/* Euler circuit (Hierholzer). Returns 1 on success and fills (path,path_len). */
int    euler_circuit(const Graph *g, int **path_out, int *path_len_out);

/* Optional debug */
void   print_graph(const Graph *g);

/* ---- New: MST (Prim, O(V^2)) ----
   Returns total MST weight, or -1 if the graph is not fully connected
   (e.g., any isolated vertex or disconnected). */
long long mst_weight_prim(const Graph *g);

// ---- Max Clique (Bron–Kerbosch with pivot) ----
// Returns the size of a maximum clique.
// If clique_out != NULL, writes the vertex ids of one maximum clique
// into clique_out (up to V entries). clique_size_out (if not NULL) gets the size.
int max_clique(const Graph *g, int *clique_out, int *clique_size_out);