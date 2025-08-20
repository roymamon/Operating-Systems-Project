// algo.h — Strategy interface + Factory for graph algorithms
#pragma once
#include <stddef.h>
#include "graph.h"

// Emit function used by strategies to write output (server provides it).
typedef int (*EmitFn)(void *ctx, const char *text);

typedef struct AlgoStrategy {
    const char *name;                  // e.g., "EULER"
    void (*run)(const Graph *g, EmitFn emit, void *ctx); // executes and emits results
} AlgoStrategy;

// Simple factory: returns a strategy by command name (EULER, MST, MAXCLIQUE, COUNTCLQ3P, HAMILTON)
// Returns NULL if not found.
const AlgoStrategy* make_strategy(const char *cmd);