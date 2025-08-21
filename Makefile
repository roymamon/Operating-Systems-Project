# Makefile — builds graph CLI, server (factory+strategy), client

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
LDFLAGS = -lm -pthread

all: graph server client

# Standalone CLI (your existing graph main)
graph: graph.c graph.h
	$(CC) $(CFLAGS) -o $@ graph.c $(LDFLAGS)

# Object without main for linking into server
graph_obj.o: graph.c graph.h
	$(CC) $(CFLAGS) -DGRAPH_NO_MAIN -c graph.c -o $@

algo.o: algo.c algo.h graph.h
	$(CC) $(CFLAGS) -c algo.c -o $@

server: server.c algo.o graph_obj.o algo.h graph.h
	$(CC) $(CFLAGS) -o $@ server.c algo.o graph_obj.o $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c $(LDFLAGS)

# Instrumented builds (optional)
graph_gprof: graph.c graph.h
	$(CC) $(CFLAGS) -pg -O2 -o $@ graph.c $(LDFLAGS)

graph_cov: graph.c graph.h
	$(CC) $(CFLAGS) --coverage -O0 -o $@ graph.c $(LDFLAGS)

clean:
	rm -f graph server client graph_gprof graph_cov \
	      *.o gmon.out *.gcno *.gcda *.gcov

.PHONY: all clean