CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -pedantic

all: cr-gen cr-check

cr_tables.h: mktables tables/TOK.bin tables/LL.bin tables/ML.bin tables/LIT.bin tables/OWN.bin
	./mktables tables cr_tables.h

mktables: mktables.c
	$(CC) $(CFLAGS) -o $@ mktables.c

cr.o: cr.c cr.h cr_tables.h
	$(CC) $(CFLAGS) -c -o $@ cr.c

cr-gen: cr-gen.c cr.o cr.h
	$(CC) $(CFLAGS) -o $@ cr-gen.c cr.o

cr-check: cr-check.c cr_tables.h
	$(CC) $(CFLAGS) -o $@ cr-check.c

clean:
	rm -f mktables cr_tables.h cr.o cr-gen cr-check

.PHONY: all clean
