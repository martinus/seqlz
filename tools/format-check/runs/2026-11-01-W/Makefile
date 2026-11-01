CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

all: cr-gen cr-check cr-test

cr_tables.h: gen_tables.c tables/TOK.bin tables/LL.bin tables/ML.bin tables/LIT.bin tables/OWN.bin
	$(CC) $(CFLAGS) -o gen_tables gen_tables.c
	./gen_tables tables cr_tables.h

cr.o: cr.c cr.h cr_features.def cr_tables.h
	$(CC) $(CFLAGS) -c cr.c

cr-gen: cr_gen.c cr.o cr.h cr_features.def
	$(CC) $(CFLAGS) -o $@ cr_gen.c cr.o

cr-check: cr_check.c cr_tables.h
	$(CC) $(CFLAGS) -o $@ cr_check.c

clean:
	rm -f gen_tables cr_tables.h cr.o cr-gen cr-check cr-test

.PHONY: all clean test

cr-test: cr_test.c cr_check.c cr.o cr.h cr_tables.h
	$(CC) $(CFLAGS) -o $@ cr_test.c cr.o

test: cr-test cr-check
	./cr-test
