// SPDX-License-Identifier: MIT OR GPL-2.0-only
// Step 6 of docs/format-check.md: for each rule of the clean-room agent I's list,
// tools/format-check/cleanroom/I/RULES.md, a block that is valid and as close to the rule as it gets, and
// the same block broken by that rule. The verdict each must get is the list's, not a decoder's, and every
// decoder must give it: seqlz_decompress(), seqlz_ref.c, and the clean-room D and R, whose cr_decode()
// CMake renames. The IDs are the list's.
#include "seqlz_blocks.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

extern "C" {
int cr_decode_d(unsigned char const* src, std::size_t len, unsigned char* out);
int cr_decode_r(unsigned char const* src, std::size_t len, unsigned char* out);
}

namespace {

using bytes = std::vector<unsigned char>;

// docs/format.md's example, ab 2048 times
bytes const example = {0x02, 0x00, 0x61, 0x62, 0x23, 0xf2, 0xff, 0xfb, 0x60};

bytes example_page() {
    auto page = bytes(SEQLZ_BLOCK_SIZE);
    for (unsigned int k = 0; k < SEQLZ_BLOCK_SIZE; ++k) {
        page[k] = k % 2 ? 'b' : 'a';
    }
    return page;
}

// Every decoder on c, each from an allocation of exactly its size: they must say the same, and decode a
// valid block to the same page, page if it is given. Returns whether they call it valid.
bool valid(seqlz_ref const& r, bytes const& c, bytes const* page = nullptr) {
    auto in = std::make_unique<unsigned char[]>(c.empty() ? 1 : c.size());
    std::copy(c.begin(), c.end(), in.get());
    auto out = std::array<bytes, 4>{};
    for (auto& o : out) {
        o.resize(SEQLZ_BLOCK_SIZE);
    }
    auto scratch = bytes(SEQLZ_SCRATCH);
    auto const said = std::array<bool, 4>{
        seqlz_decompress(in.get(), static_cast<unsigned int>(c.size()), out[0].data(), scratch.data()) == 0,
        seqlz_ref_decode(&r, in.get(), c.size(), out[1].data()) == 0,
        cr_decode_d(in.get(), c.size(), out[2].data()) == 0,
        cr_decode_r(in.get(), c.size(), out[3].data()) == 0};
    for (std::size_t k = 1; k < said.size(); ++k) {
        CAPTURE(k);
        CHECK(said[k] == said[0]);
        if (said[k] && said[0]) {
            CHECK(out[k] == out[0]);
        }
    }
    if (said[0] && page) {
        CHECK(out[0] == *page);
    }
    return said[0];
}

// The rest of the block after n literals, as the bits of one sequence: all n literals, then a match of
// the rest from 1 back, class 0 with the offset 1 a block starts with. None for n = BLOCK.
bytes rest_of_block(seqlz_ref const& r, unsigned int n) {
    auto w = bit_writer{};
    if (n == SEQLZ_BLOCK_SIZE) {
        return {};
    }
    auto const ml = SEQLZ_BLOCK_SIZE - n;
    auto const llf = std::min(n, 15U);
    auto const mlf = std::min(ml - 4U, 31U);
    w.token(r.tok, llf + 16U * mlf);
    if (llf == 15) {
        w.value(r.ll, n - 15U);
    }
    if (mlf == 31) {
        w.value(r.ml, ml - 35U);
    }
    return w.bytes;
}

// The page of rest_of_block(): the literals, then the last one again
bytes repeat_last(bytes const& literals) {
    auto page = literals;
    page.resize(SEQLZ_BLOCK_SIZE, literals.back());
    return page;
}

bytes raw_block(bytes const& literals, bytes const& b) {
    auto c = bytes{static_cast<unsigned char>(literals.size()), static_cast<unsigned char>(literals.size() >> 8)};
    c.insert(c.end(), literals.begin(), literals.end());
    c.insert(c.end(), b.begin(), b.end());
    return c;
}

// A block with coded literals as docs/format.md lays it out: the n stored literals in 8 streams, coded with
// code, which is literal table code_number, 0 to 7, or 8 with the block's own table desc; with dt 1 to 3
// the literals are deltas (e = 1), with dt 0 not. edit may change the streams before they go in, and the
// sizes take w bits, at least min_w.
bytes coded_block(seqlz_ref const& r,
                  bytes const& stored,
                  unsigned int code_number,
                  seqlz_ref_code const& code,
                  bytes const& desc,
                  int dt,
                  std::function<void(std::array<bytes, 8>&)> const& edit = {},
                  unsigned int min_w = 5) {
    auto const n = static_cast<unsigned int>(stored.size());
    auto writers = std::array<bit_writer, 8>{};
    for (unsigned int k = 0; k < n; ++k) {
        writers[k % 8].symbol(code, stored[k]);
    }
    auto streams = std::array<bytes, 8>{};
    for (unsigned int j = 0; j < 8; ++j) {
        streams[j] = writers[j].bytes;
    }
    if (edit) {
        edit(streams);
    }
    auto w = min_w;
    for (auto const& s : streams) {
        w = std::max(w, static_cast<unsigned int>(std::bit_width(s.size())));
    }
    auto c = bytes{static_cast<unsigned char>(n),
                   static_cast<unsigned char>(0x80 | n >> 8),
                   static_cast<unsigned char>(code_number | (w - 5U) << 4 | (dt ? 0x80U : 0U))};
    if (dt) {
        c.push_back(static_cast<unsigned char>(dt));
    }
    // the sizes, one little endian number of 8 * w bits, s[0] in its lowest bits
    auto S = bytes(w);
    for (unsigned int j = 0; j < 8; ++j) {
        for (unsigned int i = 0; i < w; ++i) {
            auto const q = j * w + i;
            S[q / 8] = static_cast<unsigned char>(S[q / 8] | ((streams[j].size() >> i) & 1U) << (q % 8));
        }
    }
    c.insert(c.end(), S.begin(), S.end());
    c.insert(c.end(), desc.begin(), desc.end());
    for (auto const& s : streams) {
        c.insert(c.end(), s.begin(), s.end());
    }
    auto const b = rest_of_block(r, n);
    c.insert(c.end(), b.begin(), b.end());
    return c;
}

bytes coded_block(seqlz_ref const& r,
                  bytes const& stored,
                  int dt = 0,
                  std::function<void(std::array<bytes, 8>&)> const& edit = {}) {
    return coded_block(r, stored, 0, r.lit[0], {}, dt, edit);
}

// the bytes of the header up to the sizes' end: 3 + w + e
std::size_t header_size(bytes const& c) {
    return 3U + 5U + ((c[2] >> 4) & 7U) + (c[2] >> 7);
}

bytes cut(bytes const& c, std::size_t len) {
    return {c.begin(), c.begin() + static_cast<std::ptrdiff_t>(len)};
}

bytes with(bytes c, std::size_t at, unsigned char v) {
    c[at] = v;
    return c;
}

} // namespace

TEST_CASE("format rules: the raw example of docs/format.md, and each rule that one byte of it breaks") {
    auto const r = make_ref();
    auto const page = example_page();
    REQUIRE(valid(*r, example, &page));
    SUBCASE("R1 len < 2") {
        CHECK_FALSE(valid(*r, cut(example, 1)));
        CHECK_FALSE(valid(*r, {}));
    }
    SUBCASE("R2 n > BLOCK, with R3") {
        CHECK_FALSE(valid(*r, with(example, 1, 0x10)));
    }
    SUBCASE("R3 raw literals past len") {
        CHECK_FALSE(valid(*r, cut(example, 3)));
    }
    SUBCASE("R11 a bit past the end of B") {
        CHECK_FALSE(valid(*r, cut(example, example.size() - 1)));
    }
    SUBCASE("R12 a whole byte after the last bit") {
        auto c = example;
        c.push_back(0);
        CHECK_FALSE(valid(*r, c));
    }
    SUBCASE("R12b a padding bit that is not 0") {
        CHECK_FALSE(valid(*r, with(example, 8, 0x61)));
    }
    SUBCASE("R13 an escaped token of 3072 or more") {
        // 0010 and then 3072 in 12 bits, the edge, and 4082
        CHECK_FALSE(valid(*r, with(with(example, 4, 0x2c), 5, 0x00)));
        CHECK_FALSE(valid(*r, with(example, 4, 0x2f)));
    }
    SUBCASE("R14 offset 0, the bit of class 1 is 0") {
        CHECK_FALSE(valid(*r, with(example, 6, 0x7f)));
    }
    SUBCASE("R15 an offset past |out|: ll = 1, offset 2") {
        CHECK_FALSE(valid(*r, with(example, 5, 0xf1)));
    }
    SUBCASE("R16 a match past the end of the block: ml = 4095") {
        CHECK_FALSE(valid(*r, with(example, 8, 0x80)));
    }
    SUBCASE("R17 more literals than are left: ll = 3") {
        CHECK_FALSE(valid(*r, with(example, 5, 0xf3)));
    }
}

TEST_CASE("format rules: R18, a sequence with more literals than the block has bytes left") {
    auto const r = make_ref();
    // the example's sequence, ab to the end of the block, with a third literal that is left over: a
    // token with ll = 1 for it, class 2 and offset 1, where the block has no byte left
    auto w = bit_writer{};
    w.symbol(r->tok, SEQLZ_ESCAPE);
    w.put(1010, 12); // ll = 2, mlf = 31, class 1
    w.put(1, 1);     // offset 2
    w.value(r->ml, SEQLZ_BLOCK_SIZE - 2U - 35U);
    auto const page = example_page();
    CHECK(valid(*r, raw_block({'a', 'b'}, w.bytes), &page));
    w.token(r->tok, 1 + 512 * 2);
    w.put(1, 8);
    CHECK_FALSE(valid(*r, raw_block({'a', 'b', 'c'}, w.bytes)));
}

TEST_CASE("format rules: I1, n from 1 to BLOCK - 4, or BLOCK") {
    auto const r = make_ref();
    for (unsigned int n : {1U, 2U, 4091U, 4092U}) {
        CAPTURE(n);
        auto const literals = bytes(n, 'x');
        auto const page = repeat_last(literals);
        CHECK(valid(*r, raw_block(literals, rest_of_block(*r, n)), &page));
    }
    auto const all = bytes(SEQLZ_BLOCK_SIZE, 'x');
    CHECK(valid(*r, raw_block(all, {}), &all));
    // n = 0: the first match has no byte to copy; n = 4093 to 4095: no match fits, as it is at least 4
    CHECK_FALSE(valid(*r, raw_block({}, {})));
    CHECK_FALSE(valid(*r, raw_block({}, rest_of_block(*r, 0))));
    for (unsigned int n : {4093U, 4094U, 4095U}) {
        CAPTURE(n);
        auto w = bit_writer{};
        w.token(r->tok, 15 + 16 * 0);
        w.value(r->ll, n - 15U);
        CHECK_FALSE(valid(*r, raw_block(bytes(n, 'x'), w.bytes)));
    }
}

TEST_CASE("format rules: V15 and R15, an offset of |out| and one more") {
    auto const r = make_ref();
    // 4 literals, then a match of the rest from 4 back, class 2; from 5 back is past the block's start
    for (unsigned int off : {4U, 5U}) {
        auto w = bit_writer{};
        w.token(r->tok, 4 + 16 * 31 + 512 * 2);
        w.put(off, 8);
        w.value(r->ml, SEQLZ_BLOCK_SIZE - 4U - 35U);
        auto const c = raw_block({1, 2, 3, 4}, w.bytes);
        if (off == 4) {
            auto page = bytes(SEQLZ_BLOCK_SIZE);
            for (unsigned int k = 0; k < SEQLZ_BLOCK_SIZE; ++k) {
                page[k] = static_cast<unsigned char>(1 + k % 4);
            }
            CHECK(valid(*r, c, &page));
        } else {
            CHECK_FALSE(valid(*r, c));
        }
    }
}

TEST_CASE("format rules: V1, a token escaped although it has a code") {
    auto const r = make_ref();
    auto const tok = 1U + 16U * 31U;
    REQUIRE(seqlz_default_lengths.token[tok] != 0);
    auto w = bit_writer{};
    w.symbol(r->tok, SEQLZ_ESCAPE);
    w.put(tok, 12);
    w.value(r->ml, SEQLZ_BLOCK_SIZE - 1U - 35U);
    auto const page = repeat_last({7});
    CHECK(valid(*r, raw_block({7}, w.bytes), &page));
}

TEST_CASE("format rules: coded literals, R4 to R7, R9, R19 to R21, and V5, V13, V14") {
    auto const r = make_ref();
    auto const literals = bytes(100, 'a');
    auto const page = repeat_last(literals);
    auto const good = coded_block(*r, literals);
    REQUIRE(valid(*r, good, &page));
    SUBCASE("R4 no byte 2") {
        CHECK_FALSE(valid(*r, cut(good, 2)));
    }
    SUBCASE("R5 the sizes cut off") {
        CHECK_FALSE(valid(*r, cut(good, header_size(good) - 1)));
    }
    SUBCASE("R6 dt not 1, 2 or 3") {
        // stored as deltas to the byte 4 back: the first 4 as they are, then 0
        auto stored = literals;
        std::fill(stored.begin() + 4, stored.end(), 0);
        CHECK(valid(*r, coded_block(*r, stored, 1), &page));
        CHECK(valid(*r, coded_block(*r, stored, 3), &page)); // last is 1 before the first match: 4 back
        for (int dt : {4, 0x7f}) {
            CAPTURE(dt);
            CHECK_FALSE(valid(*r, coded_block(*r, stored, dt)));
        }
        // dt = 0 with e = 1
        auto zero = coded_block(*r, stored, 1);
        zero[3] = 0;
        CHECK_FALSE(valid(*r, zero));
    }
    SUBCASE("R7 code 9 to 15") {
        for (unsigned int code : {9U, 15U}) {
            CAPTURE(code);
            CHECK_FALSE(valid(*r, with(good, 2, static_cast<unsigned char>((good[2] & 0xf0U) | code))));
        }
    }
    SUBCASE("R9 the streams past len, and V13 a block of coded literals only") {
        auto const all = bytes(SEQLZ_BLOCK_SIZE, 'a');
        auto const only = coded_block(*r, all);
        CHECK(valid(*r, only, &all));
        CHECK_FALSE(valid(*r, cut(only, only.size() - 1)));
    }
    SUBCASE("R19 a stream one byte longer than its codes") {
        CHECK_FALSE(valid(*r, coded_block(*r, literals, 0, [](std::array<bytes, 8>& s) {
            s[0].push_back(0);
        })));
    }
    SUBCASE("R20 a bit after a stream's last code that is not 0") {
        // stream 0 holds (n + 7) / 8 codes of 'a': where their bits are no multiple of 8, the lowest
        // bit of its last byte is a padding bit
        auto const l = seqlz_lit_sets[0]['a'];
        auto tried = 0;
        for (unsigned int n = 16; n < 200 && tried < 3; ++n) {
            if ((n + 7) / 8 * l % 8 == 0) {
                continue;
            }
            ++tried;
            CAPTURE(n);
            auto const lits = bytes(n, 'a');
            auto const p = repeat_last(lits);
            CHECK(valid(*r, coded_block(*r, lits), &p));
            CHECK_FALSE(valid(*r, coded_block(*r, lits, 0, [](std::array<bytes, 8>& s) {
                s[0].back() = static_cast<unsigned char>(s[0].back() | 1U);
            })));
        }
        CHECK(tried == 3);
    }
    SUBCASE("R21 a stream without literals with a size, and V14 coded literals with n < 8") {
        for (unsigned int n : {1U, 7U}) {
            CAPTURE(n);
            auto const lits = bytes(n, 'a');
            auto const p = repeat_last(lits);
            CHECK(valid(*r, coded_block(*r, lits), &p));
            CHECK_FALSE(valid(*r, coded_block(*r, lits, 0, [](std::array<bytes, 8>& s) {
                s[7].push_back(0);
            })));
        }
    }
    SUBCASE("V5 a w larger than the sizes need") {
        // the 100 literals' sizes need 5 bits; 12 are valid too
        REQUIRE(header_size(good) == 8);
        for (unsigned int w : {6U, 11U, 12U}) {
            CAPTURE(w);
            auto const wide = coded_block(*r, literals, 0, r->lit[0], {}, 0, {}, w);
            REQUIRE(header_size(wide) == 3 + w);
            CHECK(valid(*r, wide, &page));
        }
    }
}

TEST_CASE("format rules: a block's own literal table, R8a to R8e and R7 from 8 to 9") {
    auto const r = make_ref();
    for (auto const& [x, y] : {std::pair{3U, 5U}, std::pair{97U, 98U}, std::pair{0U, 255U}}) {
        CAPTURE(x);
        // x gets code 0, y code 1, as make_own_table() writes them
        auto code = seqlz_ref_code{};
        code.count[1] = 2;
        code.sym[0] = static_cast<unsigned short>(x);
        code.sym[1] = static_cast<unsigned short>(y);
        auto lits = bytes(64);
        for (unsigned int k = 0; k < lits.size(); ++k) {
            lits[k] = static_cast<unsigned char>(k % 3 ? x : y);
        }
        auto const page = repeat_last(lits);
        auto padding = 0U;
        auto const desc = make_own_table(*r, x, y, own_damage::none, &padding);
        auto const good = coded_block(*r, lits, 8, code, desc, 0);
        REQUIRE(valid(*r, good, &page));
        auto unused = 0U;
        SUBCASE("R8a the code space overfull") {
            CHECK_FALSE(valid(*r, coded_block(*r, lits, 8, code, make_own_table(*r, x, y, own_damage::overfull, &unused), 0)));
        }
        SUBCASE("R8b a code for byte value 256") {
            CHECK_FALSE(
                valid(*r, coded_block(*r, lits, 8, code, make_own_table(*r, x, y, own_damage::above_255, &unused), 0)));
        }
        SUBCASE("R8c a run past 255") {
            CHECK_FALSE(
                valid(*r, coded_block(*r, lits, 8, code, make_own_table(*r, x, y, own_damage::run_past_255, &unused), 0)));
        }
        SUBCASE("R8d a bit after the table's last symbol that is not 0") {
            if (padding > 0) {
                CHECK_FALSE(
                    valid(*r, coded_block(*r, lits, 8, code, make_own_table(*r, x, y, own_damage::padding_bit, &unused), 0)));
            }
        }
        SUBCASE("R8e the block ends in the table") {
            CHECK_FALSE(valid(*r, cut(good, header_size(good) + 1)));
        }
        SUBCASE("R7 code 9 instead of 8") {
            CHECK_FALSE(valid(*r, with(good, 2, static_cast<unsigned char>((good[2] & 0xf0U) | 9U))));
        }
    }
}
