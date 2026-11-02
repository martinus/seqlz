# make test: builds the tables from tables/, then the tests with ASan+UBSan, with MSan, and
# optimized, and runs them all.
CC ?= cc
CLANG ?= clang
CFLAGS_COMMON = -std=c11 -Wall -Wextra -pedantic -g
SRC = src/cr_decode.c
DEPS = src/cr_decode.c src/cr_decode.h src/cr_tables.h tests/test_decode.c

.PHONY: test clean fuzz fuzz-msan

test: build/test_asan build/test_msan build/test_opt
	./build/test_asan 20000
	./build/test_msan 5000
	./build/test_opt 20000

src/cr_tables.h: tools/gen_tables.py tables/TOK.bin tables/LL.bin tables/ML.bin tables/LIT.bin tables/OWN.bin
	python3 -I tools/gen_tables.py tables $@

build:
	mkdir -p build

build/test_asan: $(DEPS) | build
	$(CLANG) $(CFLAGS_COMMON) -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -o $@ $(SRC) tests/test_decode.c

build/test_msan: $(DEPS) | build
	$(CLANG) $(CFLAGS_COMMON) -O1 -fsanitize=memory -fno-sanitize-recover=all -fno-omit-frame-pointer -o $@ $(SRC) tests/test_decode.c

build/test_opt: $(DEPS) | build
	$(CC) $(CFLAGS_COMMON) -O2 -Werror -o $@ $(SRC) tests/test_decode.c

# libFuzzer with ASan+UBSan; seeds are made by the test (build/corpus); runs 60 s
fuzz: build/test_opt
	$(CLANG) $(CFLAGS_COMMON) -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all -o build/fuzz $(SRC) tests/fuzz.c
	mkdir -p build/corpus
	./build/test_opt 100 build/corpus > /dev/null
	./build/fuzz -max_total_time=60 -max_len=8192 build/corpus

clean:
	rm -rf build

# libFuzzer with MSan, 30 s
fuzz-msan: build/test_opt
	$(CLANG) $(CFLAGS_COMMON) -O1 -fsanitize=fuzzer,memory -o build/fuzz_msan $(SRC) tests/fuzz.c
	mkdir -p build/corpus
	./build/test_opt 100 build/corpus > /dev/null
	./build/fuzz_msan -max_total_time=30 -max_len=8192 build/corpus
