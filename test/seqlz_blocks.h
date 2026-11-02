// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Blocks made by hand, as docs/format.md writes them, for test/seqlz_ref_test.cpp and
// test/seqlz_rules_test.cpp: a writer of the bits, coded literals, a block's own literal table. inline,
// so that each test file takes what it needs.
#ifndef SEQLZ_TEST_SEQLZ_BLOCKS_H
#define SEQLZ_TEST_SEQLZ_BLOCKS_H

// seqlz.h is C, written for the kernel
extern "C" {
#include "seqlz.h"
}
#include "seqlz_ref.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <memory>
#include <vector>

inline seqlz_ref_tables format_tables() {
    return {
        seqlz_default_lengths.token, seqlz_default_lengths.ll, seqlz_default_lengths.ml, seqlz_lit_sets, seqlz_own_lengths};
}

// The bits of each stream's size, from byte 2 of a page with coded literals.
inline unsigned int size_width(std::vector<unsigned char> const& c) {
    return SEQLZ_SIZE_BITS_MIN + ((c[2] >> SEQLZ_LIT_WIDTH_AT) & 7U);
}

inline std::unique_ptr<seqlz_ref> make_ref() {
    auto r = std::make_unique<seqlz_ref>();
    auto const t = format_tables();
    REQUIRE(seqlz_ref_init(r.get(), &t) == 0);
    return r;
}

// Bits most significant first, as docs/format.md writes the bitstream, for pages made by hand.
struct bit_writer {
    std::vector<unsigned char> bytes;
    unsigned int n = 0;

    void put(unsigned int v, unsigned int bits) {
        for (unsigned int k = bits; k-- > 0;) {
            if (n % 8 == 0) {
                bytes.push_back(0);
            }
            bytes.back() = static_cast<unsigned char>(bytes.back() | ((v >> k) & 1U) << (7 - n % 8));
            ++n;
        }
    }

    // a symbol's canonical code, from the codes the reference decoder built
    void symbol(seqlz_ref_code const& c, unsigned int sym) {
        for (unsigned int l = 1; l < 16; ++l) {
            for (unsigned int i = 0; i < c.count[l]; ++i) {
                if (c.sym[c.start[l] + i] == sym) {
                    put(c.first[l] + i, l);
                    return;
                }
            }
        }
        FAIL("no code for symbol ", sym);
    }

    // a length value: below 16 its own symbol, else the position of its top bit and the bits below
    void value(seqlz_ref_code const& c, unsigned int v) {
        if (v < 16) {
            symbol(c, v);
            return;
        }
        auto const b = static_cast<unsigned int>(std::bit_width(v)) - 1U;
        symbol(c, 12 + b);
        put(v - (1U << b), b);
    }

    // a token, escaped when it has no code
    void token(seqlz_ref_code const& c, unsigned int tok) {
        if (seqlz_default_lengths.token[tok] != 0) {
            symbol(c, tok);
        } else {
            symbol(c, SEQLZ_ESCAPE);
            put(tok, 12);
        }
    }
};

// literals raw literals, k * 7 for literal k, and no bits: valid only with PAGE of them.
inline std::vector<unsigned char> literals_only(unsigned int literals) {
    auto c_page = std::vector<unsigned char>{static_cast<unsigned char>(literals), static_cast<unsigned char>(literals >> 8)};
    for (unsigned int k = 0; k < literals; ++k) {
        c_page.push_back(static_cast<unsigned char>(k * 7));
    }
    return c_page;
}

// A page of n literals literal_of(k), coded with literal table 0, or with code and the page's own table
// desc, then a match from 1 back to the end of the page; valid if the last literal repeats as the match
// needs, as for literals all the same.
template <typename F>
std::vector<unsigned char> coded_literals(
    seqlz_ref const& r, unsigned int n, F literal_of, seqlz_ref_code const& code, std::vector<unsigned char> const& desc) {
    auto streams = std::vector<bit_writer>(8);
    for (unsigned int k = 0; k < n; ++k) {
        streams[k % 8].symbol(code, literal_of(k));
    }
    auto w = 5U;
    for (auto const& s : streams) {
        w = std::max(w, static_cast<unsigned int>(std::bit_width(s.bytes.size())));
    }
    auto c = std::vector<unsigned char>{
        static_cast<unsigned char>(n),
        static_cast<unsigned char>(0x80 | n >> 8),
        static_cast<unsigned char>((w - SEQLZ_SIZE_BITS_MIN) << SEQLZ_LIT_WIDTH_AT | (desc.empty() ? 0U : SEQLZ_LIT_OWN))};
    auto sizes = bit_writer{};
    for (int j = 7; j >= 0; --j) {
        sizes.put(static_cast<unsigned int>(streams[static_cast<std::size_t>(j)].bytes.size()), w);
    }
    // the sizes are one little endian number, s[0] in the lowest bits: the bytes of the big endian
    // writer the other way round
    c.insert(c.end(), sizes.bytes.rbegin(), sizes.bytes.rend());
    c.insert(c.end(), desc.begin(), desc.end());
    for (auto const& s : streams) {
        c.insert(c.end(), s.bytes.begin(), s.bytes.end());
    }
    auto seq = bit_writer{};
    // ll and ml - 4 at their caps, class 0 with the offset 1 a page starts with
    seq.token(r.tok, 15 + 16 * 31);
    seq.value(r.ll, n - 15);
    seq.value(r.ml, SEQLZ_BLOCK_SIZE - n - 35);
    c.insert(c.end(), seq.bytes.begin(), seq.bytes.end());
    return c;
}

template <typename F>
std::vector<unsigned char> coded_literals(seqlz_ref const& r, unsigned int n, F literal_of) {
    return coded_literals(r, n, literal_of, r.lit[0], {});
}

// n literals b: stream j has the codes of the literals j, j + 8, ..., all of the same length, so as n
// grows each stream's bits end at every place of a byte.
inline std::vector<unsigned char> coded_run(seqlz_ref const& r, unsigned int n, unsigned int b) {
    return coded_literals(r, n, [b](unsigned int) {
        return b;
    });
}

// What make_own_table() does to the table it writes
enum class own_damage { none, overfull, above_255, run_past_255, padding_bit };

// A page's own table in which only the bytes x < y have a code, of 1 bit each: the runs before them, as
// docs/format.md has them, and their lengths. *padding gets the bits after its last symbol.
inline std::vector<unsigned char>
make_own_table(seqlz_ref const& r, unsigned int x, unsigned int y, own_damage damage, unsigned int* padding) {
    auto w = bit_writer{};
    auto run = [&](unsigned int n) {
        if (n > 0) {
            auto const k = static_cast<unsigned int>(std::bit_width(n));
            w.symbol(r.own, SEQLZ_OWN_RUN + k);
            w.put(n - (1U << (k - 1)), k - 1);
        }
    };
    run(x);
    // with overfull a code of 2 bits for x, then one of 1 bit for y and one more for y + 1
    w.symbol(r.own, damage == own_damage::overfull ? 1 : 0);
    // above_255: the run ends at 255, so y's code is for 256; run_past_255: the run itself ends at 256
    run(damage == own_damage::above_255 ? 255 - x : damage == own_damage::run_past_255 ? 256 - x : y - x - 1);
    w.symbol(r.own, 0);
    if (damage == own_damage::overfull) {
        w.symbol(r.own, 0);
    }
    *padding = 8 * static_cast<unsigned int>(w.bytes.size()) - w.n;
    if (damage == own_damage::padding_bit) {
        w.bytes.back() |= 1U;
    }
    return w.bytes;
}

#endif
