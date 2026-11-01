#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include "enc.h"

int cr_decode(const unsigned char *src, size_t len, unsigned char *out);

static int fails = 0, passes = 0;

static void expect_valid(const char *name, const unsigned char *src, size_t len,
                         const unsigned char *want)
{
    unsigned char out[4096];
    memset(out, 0xCD, sizeof out);
    int r = cr_decode(src, len, out);
    if (r != 0) { fails++; printf("FAIL: %s: expected valid, got -1\n", name); return; }
    if (want && memcmp(out, want, 4096) != 0) {
        fails++; printf("FAIL: %s: output mismatch\n", name);
        for (int i = 0; i < 4096; i++) if (out[i] != want[i]) {
            printf("  first diff at %d: got %02x want %02x\n", i, out[i], want[i]); break; }
        return;
    }
    passes++;
}
static void expect_invalid(const char *name, const unsigned char *src, size_t len)
{
    unsigned char out[4096];
    int r = cr_decode(src, len, out);
    if (r == -1) passes++;
    else { fails++; printf("FAIL: %s: expected invalid, got 0\n", name); }
}

/* emit an escaped token (always legal) */
static void emit_token(BW *B, Canon *tok, int ll, int mlf, int c)
{ bw_sym(B, tok, 3072); bw_bits(B, (uint32_t)(ll | (mlf << 4) | (c << 9)), 12); }

int main(void)
{
    Canon tok, lit0, own;
    canon(&tok, seqlz_tok_len, 3073);
    canon(&lit0, seqlz_lit_len[0], 256);
    canon(&own, seqlz_own_len, 19);

    int litb = -1;      /* a byte value with a code in literal table 0 */
    for (int i = 0; i < 256; i++) if (seqlz_lit_len[0][i]) { litb = i; break; }

    /* ---- spec example ---- */
    {
        unsigned char src[] = {0x02,0x00,0x61,0x62,0x23,0xf2,0xff,0xfb,0x60};
        unsigned char want[4096];
        for (int i = 0; i < 4096; i++) want[i] = (i & 1) ? 'b' : 'a';
        expect_valid("spec example", src, sizeof src, want);
    }

    /* ---- A: raw literals only (n=4096, no bitstream) ---- */
    {
        unsigned char src[2 + 4096]; unsigned char want[4096];
        src[0] = 0x00; src[1] = 0x10;           /* n = 4096, raw */
        for (int i = 0; i < 4096; i++) { src[2+i] = (unsigned char)(i * 7 + 1); want[i] = src[2+i]; }
        expect_valid("raw literals only", src, sizeof src, want);
    }

    /* ---- B: raw, one match sequence + final literal run ---- */
    {
        /* seq1: ll=10 'A', match off=1 ml=30 -> 40 'A'; then 4056 'B' literals */
        int n = 4066;
        unsigned char src[2 + 4066 + 8]; unsigned char want[4096];
        src[0] = (unsigned char)(n & 0xff); src[1] = (unsigned char)(n >> 8);
        for (int i = 0; i < 10; i++) src[2+i] = 'A';
        for (int i = 10; i < n; i++) src[2+i] = 'B';
        BW B; bw_init(&B, src + 2 + n, 8);
        emit_token(&B, &tok, 10, 26, 2);        /* ll10 mlf26 class2 */
        bw_bits(&B, 1, 8);                       /* class2 offset = 1 */
        bw_pad_byte(&B);
        size_t blen = 2 + n + (B.nbits + 7) / 8;
        for (int i = 0; i < 40; i++) want[i] = 'A';
        for (int i = 40; i < 4096; i++) want[i] = 'B';
        expect_valid("raw one match + final run", src, blen, want);

        /* B-invalid: offset 0 */
        {
            unsigned char s2[2 + 4066 + 8]; memcpy(s2, src, blen);
            BW B2; bw_init(&B2, s2 + 2 + n, 8);
            emit_token(&B2, &tok, 10, 26, 2); bw_bits(&B2, 0, 8); bw_pad_byte(&B2);
            expect_invalid("offset 0", s2, 2 + n + (B2.nbits + 7) / 8);
        }
        /* B-invalid: extra trailing zero byte */
        {
            unsigned char s3[2 + 4066 + 8]; memcpy(s3, src, blen); s3[blen] = 0x00;
            expect_invalid("extra trailing byte", s3, blen + 1);
        }
        /* B-invalid: trailing non-zero bit (from spec example padding) */
        {
            unsigned char s4[] = {0x02,0x00,0x61,0x62,0x23,0xf2,0xff,0xfb,0x61};
            expect_invalid("trailing nonzero bit", s4, sizeof s4);
        }
    }

    /* ---- C: coded literals only, fixed table 0, e=0 ---- */
    {
        int n = 4096, w = 12, e = 0, code = 0;
        unsigned char src[8192]; unsigned char want[4096];
        unsigned char streams[8][2048]; size_t sbytes[8];
        for (int j = 0; j < 8; j++) {
            BW S; bw_init(&S, streams[j], sizeof streams[j]);
            for (int k = j; k < n; k += 8) bw_sym(&S, &lit0, litb);
            bw_pad_byte(&S); sbytes[j] = (S.nbits + 7) / 8;
        }
        for (int i = 0; i < n; i++) want[i] = (unsigned char)litb;
        src[0] = 0x00; src[1] = 0x90;            /* coded, n=4096 */
        int t = code | ((w - 5) << 4) | (e << 7); src[2] = (unsigned char)t;
        /* sizes S: 8 values of w bits, little endian from byte 3 */
        for (int j = 0; j < 8; j++)
            for (int bit = 0; bit < w; bit++) {
                size_t gbit = (size_t)j * w + bit;
                if ((sbytes[j] >> bit) & 1) src[3 + (gbit >> 3)] |= (unsigned char)(1 << (gbit & 7));
                else src[3 + (gbit >> 3)] &= (unsigned char)~(1 << (gbit & 7));
            }
        for (int k = (int)(3 + w); k < (int)(3 + w + (size_t)w*0); k++) (void)k;
        /* clear size region first */
        for (int b = 0; b < w; b++) if (b) {} /* no-op */
        /* (region was zeroed by not writing? ensure) */
        size_t pos = 3 + w;
        for (int j = 0; j < 8; j++) { memcpy(src + pos, streams[j], sbytes[j]); pos += sbytes[j]; }
        expect_valid("coded fixed table only", src, pos, want);

        /* C-invalid: wrong stream size (increment s[0]) */
        {
            unsigned char s2[8192]; memcpy(s2, src, pos);
            size_t gbit = 0; /* s[0] low bit */
            s2[3 + (gbit >> 3)] ^= 1; /* flip -> size mismatch */
            expect_invalid("coded wrong stream size", s2, pos);
        }
    }

    /* ---- D: coded literals only, block's own table (code 8) ---- */
    {
        /* own table: byte 0 len 1, byte 1 len 1 -> codes "0","1" */
        int n = 4096, w = 12, e = 0, code = 8;
        unsigned char src[8192]; unsigned char want[4096];
        unsigned char own_desc[64]; size_t own_bytes;
        unsigned char streams[8][2048]; size_t sbytes[8];
        /* own description: two symbols "code length 1" = OWN symbol 0 */
        { BW O; bw_init(&O, own_desc, sizeof own_desc);
          bw_sym(&O, &own, 0); bw_sym(&O, &own, 0); bw_pad_byte(&O);
          own_bytes = (O.nbits + 7) / 8; }
        /* own codes: length-1 codes for byte 0 ("0") and byte 1 ("1") */
        for (int j = 0; j < 8; j++) {
            BW S; bw_init(&S, streams[j], sizeof streams[j]);
            for (int k = j; k < n; k += 8) bw_bit(&S, (k & 1));  /* byte value = k&1 */
            bw_pad_byte(&S); sbytes[j] = (S.nbits + 7) / 8;
        }
        for (int i = 0; i < n; i++) want[i] = (unsigned char)(i & 1);
        src[0] = 0x00; src[1] = 0x90;
        src[2] = (unsigned char)(code | ((w - 5) << 4) | (e << 7));
        for (int j = 0; j < 8; j++)
            for (int bit = 0; bit < w; bit++) {
                size_t gbit = (size_t)j * w + bit;
                if ((sbytes[j] >> bit) & 1) src[3 + (gbit >> 3)] |= (unsigned char)(1 << (gbit & 7));
                else src[3 + (gbit >> 3)] &= (unsigned char)~(1 << (gbit & 7));
            }
        memcpy(src + 3 + w, own_desc, own_bytes);
        size_t pos = 3 + w + own_bytes;
        for (int j = 0; j < 8; j++) { memcpy(src + pos, streams[j], sbytes[j]); pos += sbytes[j]; }
        expect_valid("coded own table", src, pos, want);
    }

    /* ---- E: coded literals only with delta e=1, dt=1 (x=4) ---- */
    {
        int n = 4096, w = 12, e = 1, code = 0, dt = 1;
        unsigned char src[8192]; unsigned char want[4096];
        unsigned char streams[8][2048]; size_t sbytes[8];
        for (int j = 0; j < 8; j++) {
            BW S; bw_init(&S, streams[j], sizeof streams[j]);
            for (int k = j; k < n; k += 8) bw_sym(&S, &lit0, litb);
            bw_pad_byte(&S); sbytes[j] = (S.nbits + 7) / 8;
        }
        /* stored = litb everywhere; out[p]=litb for p<4 else (litb+out[p-4])%256 */
        for (int p = 0; p < 4096; p++)
            want[p] = (p < 4) ? (unsigned char)litb
                              : (unsigned char)((litb + want[p-4]) & 0xff);
        src[0] = 0x00; src[1] = 0x90;
        src[2] = (unsigned char)(code | ((w - 5) << 4) | (e << 7));
        src[3] = (unsigned char)dt;              /* dt byte at offset 3 (e=1) */
        for (int j = 0; j < 8; j++)
            for (int bit = 0; bit < w; bit++) {
                size_t gbit = (size_t)j * w + bit;
                if ((sbytes[j] >> bit) & 1) src[3 + e + (gbit >> 3)] |= (unsigned char)(1 << (gbit & 7));
                else src[3 + e + (gbit >> 3)] &= (unsigned char)~(1 << (gbit & 7));
            }
        size_t pos = 3 + e + w;
        for (int j = 0; j < 8; j++) { memcpy(src + pos, streams[j], sbytes[j]); pos += sbytes[j]; }
        expect_valid("coded delta dt=1", src, pos, want);

        /* E-invalid: dt = 0 */
        { unsigned char s2[8192]; memcpy(s2, src, pos); s2[3] = 0; expect_invalid("dt=0 invalid", s2, pos); }
        /* E-invalid: dt = 4 */
        { unsigned char s3[8192]; memcpy(s3, src, pos); s3[3] = 4; expect_invalid("dt=4 invalid", s3, pos); }
    }

    Canon mlc, llc;
    canon(&mlc, seqlz_ml_len, 24);
    canon(&llc, seqlz_ll_len, 24);

    /* ---- F: match reaching the block end (empty last sequence) ---- */
    {
        int n = 4;
        unsigned char src[32]; unsigned char want[4096];
        const char *WXYZ = "WXYZ";
        src[0] = (unsigned char)n; src[1] = 0;
        for (int i = 0; i < 4; i++) src[2+i] = (unsigned char)WXYZ[i];
        BW B; bw_init(&B, src + 2 + n, 16);
        emit_token(&B, &tok, 4, 31, 2);          /* ll4 mlf31 class2 */
        bw_bits(&B, 4, 8);                         /* offset 4 */
        bw_sym(&B, &mlc, 23);                      /* ML symbol 23, b=11 */
        bw_bits(&B, 2009, 11);                     /* ml = 35+2048+2009 = 4092 */
        bw_pad_byte(&B);
        size_t blen = 2 + n + (B.nbits + 7) / 8;
        for (int i = 0; i < 4096; i++) want[i] = (unsigned char)WXYZ[i & 3];
        expect_valid("match reaches end", src, blen, want);
    }

    /* ---- G: ll >= 15 using an LL value ---- */
    {
        int n = 4092;
        static unsigned char src[2 + 4092 + 16]; unsigned char want[4096];
        src[0] = (unsigned char)(n & 0xff); src[1] = (unsigned char)(n >> 8);
        for (int i = 0; i < 20; i++) src[2+i] = 'L';
        for (int i = 20; i < n; i++) src[2+i] = 'M';
        BW B; bw_init(&B, src + 2 + n, 16);
        emit_token(&B, &tok, 15, 0, 2);          /* ll=15 (extend), mlf0 class2 */
        bw_bits(&B, 1, 8);                         /* offset 1 */
        bw_sym(&B, &llc, 5);                       /* LL value 5 -> ll = 20 */
        bw_pad_byte(&B);
        size_t blen = 2 + n + (B.nbits + 7) / 8;
        for (int i = 0; i < 24; i++) want[i] = 'L';
        for (int i = 24; i < 4096; i++) want[i] = 'M';
        expect_valid("ll extended by LL value", src, blen, want);
    }

    /* ---- H: class 0 reuses the last offset ---- */
    {
        int n = 4088;
        static unsigned char src[2 + 4088 + 16]; unsigned char want[4096];
        src[0] = (unsigned char)(n & 0xff); src[1] = (unsigned char)(n >> 8);
        src[2] = 'A'; src[3] = 'B';
        for (int i = 2; i < n; i++) src[2+i] = 'C';
        BW B; bw_init(&B, src + 2 + n, 16);
        emit_token(&B, &tok, 1, 0, 2); bw_bits(&B, 1, 8);  /* seq1 ll1 ml4 off1 */
        emit_token(&B, &tok, 1, 0, 0);                     /* seq2 ll1 ml4 class0 */
        bw_pad_byte(&B);
        size_t blen = 2 + n + (B.nbits + 7) / 8;
        for (int i = 0; i < 5; i++) want[i] = 'A';
        for (int i = 5; i < 10; i++) want[i] = 'B';
        for (int i = 10; i < 4096; i++) want[i] = 'C';
        expect_valid("class 0 repeats last offset", src, blen, want);
        /* H-invalid: first-sequence offset larger than bytes written */
        {
            static unsigned char s2[2 + 4088 + 16]; memcpy(s2, src, blen);
            BW B2; bw_init(&B2, s2 + 2 + n, 16);
            emit_token(&B2, &tok, 1, 0, 2); bw_bits(&B2, 5, 8); /* off 5 > |out|=1 */
            emit_token(&B2, &tok, 1, 0, 0); bw_pad_byte(&B2);
            expect_invalid("offset > bytes written", s2, 2 + n + (B2.nbits + 7) / 8);
        }
    }

    /* ---- misc invalid blocks ---- */
    { unsigned char s[1] = {0}; expect_invalid("len<2", s, 1); }
    { unsigned char s[0]; expect_invalid("len0", s, 0); }
    { unsigned char s[2] = {0x01, 0x90}; expect_invalid("n>BLOCK", s, 2); } /* n=0x1001=4097 */
    { unsigned char s[5] = {0x0A, 0x00, 1,2,3}; expect_invalid("raw 2+n>len", s, 5); } /* n=10 */
    { unsigned char s[2] = {0x00, 0x80}; expect_invalid("coded len<3", s, 2); }
    { unsigned char s[8] = {0x00,0x80, 0x09, 0,0,0,0,0}; expect_invalid("code=9", s, 8); } /* code field 9 */
    /* own table invalid: sum>1 (two length-1 codes then another length-1) */
    {
        int w = 12, e = 0, code = 8; unsigned char src[64];
        unsigned char own_desc[16]; BW O; bw_init(&O, own_desc, sizeof own_desc);
        bw_sym(&O, &own, 0); bw_sym(&O, &own, 0); bw_sym(&O, &own, 0); bw_pad_byte(&O);
        size_t ob = (O.nbits + 7) / 8;
        memset(src, 0, sizeof src);
        src[0]=0x00; src[1]=0x80; src[2]=(unsigned char)(code|((w-5)<<4)|(e<<7));
        memcpy(src + 3 + w, own_desc, ob);
        expect_invalid("own sum>1", src, 3 + w + ob + 1);
    }

    printf("\n%d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
