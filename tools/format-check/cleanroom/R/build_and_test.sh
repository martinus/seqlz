#!/bin/sh
# Build the tables header from tables/, build the decoder and tests, run them.
# One command: ./build_and_test.sh
set -e
cd "$(dirname "$0")"

CC=${CC:-cc}
SAN="-fsanitize=address,undefined"

echo "== generating tables header =="
python3 gen_tables.py

echo "== building (with AddressSanitizer + UndefinedBehaviorSanitizer) =="
$CC -std=c11 -Wall -Wextra -O2 -g $SAN seqlz.c tests.c -o seqlz_tests

echo "== running tests =="
./seqlz_tests
