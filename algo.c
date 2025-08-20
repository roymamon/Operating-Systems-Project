// algo.c — concrete strategies using your graph.c algorithms
#define _XOPEN_SOURCE 700
#include "algo.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* small helper to emit formatted lines */
static void emitf(EmitFn emit, void *ctx, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    emit(ctx, buf);
}

/* ---------------- EULER strategy ---------------- */
static void strat_euler_run(const Graph *g, EmitFn emit, void *ctx) {
    if (!connected_among_non_isolated(g)) {
        emitf(emit, ctx, "No Euler circuit: graph is disconnected among non-isolated vertices.\n");
        return;
    }
    int odd = 0;
    for (int i = 0; i < g->V; ++i) if (degree(g, i) % 2 != 0) odd++;
    if (odd != 0) {
        emitf(emit, ctx, "No Euler circuit: %d vertices have odd degree.\n", odd);
        return;
    }

    int *path = NULL, len = 0;
    if (!euler_circuit(g, &path, &len)) {
        emitf(emit, ctx, "No Euler circuit (unexpected after checks).\n");
        return;
    }
    emitf(emit, ctx, "Euler circuit exists. Sequence of vertices:\n");
    for (int i = 0; i < len; ++i)
        emitf(emit, ctx, "%d%s", path[i], (i + 1 == len) ? "\n" : " -> ");
    free(path);
}

/* ---------------- MST strategy ---------------- */
static void strat_mst_run(const Graph *g, EmitFn emit, void *ctx) {
    long long w = mst_weight_prim(g);
    if (w < 0) emitf(emit, ctx, "MST: graph is not connected (no spanning tree)\n");
    else       emitf(emit, ctx, "MST total weight: %lld\n", w);
}

/* ---------------- MAXCLIQUE strategy ---------------- */
static void strat_maxclique_run(const Graph *g, EmitFn emit, void *ctx) {
    int *cl = (int*)malloc((size_t)g->V * sizeof(int));
    if (!cl) { emitf(emit, ctx, "ERR: out of memory\n"); return; }
    int got = 0;
    int k = max_clique(g, cl, &got);
    emitf(emit, ctx, "Max clique size = %d\n", k);
    if (got > 0) {
        emitf(emit, ctx, "Vertices: ");
        for (int i = 0; i < got; ++i)
            emitf(emit, ctx, "%d%s", cl[i], (i + 1 == got) ? "\n" : " ");
    }
    free(cl);
}

/* ---------------- COUNT CLQ >=3 strategy ---------------- */
static void strat_countclq3p_run(const Graph *g, EmitFn emit, void *ctx) {
    long long cnt = count_cliques_3plus(g);
    emitf(emit, ctx, "Number of cliques (size >= 3): %lld\n", cnt);
}

/* ---------------- HAMILTON strategy ---------------- */
static void strat_hamilton_run(const Graph *g, EmitFn emit, void *ctx) {
    int *cyc = NULL, L = 0;
    if (!hamilton_cycle(g, &cyc, &L)) {
        emitf(emit, ctx, "No Hamiltonian cycle.\n");
        return;
    }
    emitf(emit, ctx, "Hamiltonian cycle found:\n");
    for (int i = 0; i < L; ++i)
        emitf(emit, ctx, "%d%s", cyc[i], (i + 1 == L) ? "\n" : " -> ");
    free(cyc);
}

/* ---------------- Factory ---------------- */
typedef struct { const char *name; void (*run)(const Graph*, EmitFn, void*); } Pair;
static const Pair TABLE[] = {
    {"EULER",      strat_euler_run},
    {"MST",        strat_mst_run},
    {"MAXCLIQUE",  strat_maxclique_run},
    {"COUNTCLQ3P", strat_countclq3p_run},
    {"HAMILTON",   strat_hamilton_run},
};

const AlgoStrategy* make_strategy(const char *cmd) {
    static AlgoStrategy strat; // returned by address; contents filled per lookup
    for (size_t i = 0; i < sizeof(TABLE)/sizeof(TABLE[0]); ++i) {
        if (strcmp(cmd, TABLE[i].name) == 0) {
            strat.name = TABLE[i].name;
            strat.run  = TABLE[i].run;
            return &strat;
        }
    }
    return NULL;
}