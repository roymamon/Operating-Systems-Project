CC      = gcc
CFLAGS  = -Wall -Wextra -O2
LDFLAGS = -lm

all: graph server client

# Standalone CLI tool (keeps main)
graph: graph.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Object without main for server linking
graph_obj.o: graph.c graph.h
	$(CC) $(CFLAGS) -DGRAPH_NO_MAIN -c graph.c -o $@

server: server.c graph_obj.o graph.h
	$(CC) $(CFLAGS) -o $@ server.c graph_obj.o $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o $@ client.c

# Profiling/coverage builds for the CLI (optional, unchanged)
graph_gprof: graph.c
	$(CC) $(CFLAGS) -pg -O2 -o $@ $< $(LDFLAGS)

graph_cov: graph.c
	$(CC) $(CFLAGS) --coverage -O0 -o $@ $< $(LDFLAGS)

clean:
	rm -f graph server client graph_gprof graph_cov graph_obj.o \
	      gmon.out *.gcno *.gcda *.gcov

.PHONY: all clean