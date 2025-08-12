// graph.h
#pragma once
#include <stddef.h>

typedef struct {
    int V;      // number of vertices
    int E;      // number of edges
    int **adj;  // adjacency matrix (0/1), undirected
} Graph;

/* Construction / teardown */
Graph* create_graph(int V);
void   free_graph(Graph *g);

/* Random generator */
void   generate_random_graph(Graph *g, int targetE, unsigned int seed);

/* Helpers and checks */
int    degree(const Graph *g, int u);
int    connected_among_non_isolated(const Graph *g);
int    all_even_degrees(const Graph *g);

/* Euler circuit (Hierholzer). Returns 1 on success and fills (path,path_len). */
int    euler_circuit(const Graph *g, int **path_out, int *path_len_out);

/* Optional debug */
void   print_graph(const Graph *g);