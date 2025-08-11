# Makefile for Euler circuit project

CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lm

# Default build (plain)
all: graph

graph: graph.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Build with gprof instrumentation
graph_gprof: graph.c
	$(CC) $(CFLAGS) -pg -O2 -o $@ $< $(LDFLAGS)

# Build with gcov instrumentation
graph_cov: graph.c
	$(CC) $(CFLAGS) --coverage -O0 -o $@ $< $(LDFLAGS)

clean:
	rm -f graph graph_gprof graph_cov gmon.out *.gcno *.gcda *.gcov

.PHONY: all clean