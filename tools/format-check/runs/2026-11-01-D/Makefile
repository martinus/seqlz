# make test: generates the tables, builds the decoder and the tests with sanitizers, runs them.
CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion
CLANG ?= clang

all: cr_tables.h build/test_asan

cr_tables.h: tools/gen_tables.py tables/TOK.bin tables/LL.bin tables/ML.bin tables/LIT.bin tables/OWN.bin
	python3 tools/gen_tables.py tables $@

build/test_asan: cr_decode.c cr_decode.h cr_tables.h test/test_decode.c
	@mkdir -p build
	$(CC) $(CFLAGS) -g -fsanitize=address,undefined -fno-sanitize-recover=all -o $@ cr_decode.c test/test_decode.c

build/test_msan: cr_decode.c cr_decode.h cr_tables.h test/test_decode.c
	@mkdir -p build
	$(CLANG) $(CFLAGS) -g -fsanitize=memory -fno-sanitize-recover=all -o $@ cr_decode.c test/test_decode.c

build/test_fast: cr_decode.c cr_decode.h cr_tables.h test/test_decode.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ cr_decode.c test/test_decode.c

build/fuzz: cr_decode.c cr_decode.h cr_tables.h test/fuzz.c
	@mkdir -p build
	$(CLANG) -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o $@ cr_decode.c test/fuzz.c

test: build/test_fast build/test_asan build/test_msan
	./build/test_fast 20000
	./build/test_asan 5000
	./build/test_msan 2000

clean:
	rm -rf build

.PHONY: all test clean
