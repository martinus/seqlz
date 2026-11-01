#!/bin/sh
# One command: regenerate the embedded tables (checking their SHA-256),
# build the decoder and tests under ASan+UBSan, and run the tests.
set -e
cd "$(dirname "$0")"

echo "== generating tables from tables/*.bin (SHA-256 checked) =="
python3 gen_tables.py

echo "== building tests (AddressSanitizer + UndefinedBehaviorSanitizer) =="
cc -std=c11 -Wall -Wextra -O2 -g -fsanitize=address,undefined \
   cr_decode.c test_seqlz.c -o test_seqlz

echo "== running tests =="
./test_seqlz
