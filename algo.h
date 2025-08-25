#pragma once
#include <stddef.h>
#include "graph.h"

typedef int (*EmitFn)(void *ctx, const char *text);

typedef struct AlgoStrategy {
    const char *name;                  
    void (*run)(const Graph *g, EmitFn emit, void *ctx); // executes and emits results
} AlgoStrategy;


const AlgoStrategy* make_strategy(const char *cmd);