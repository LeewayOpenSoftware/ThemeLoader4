/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Unit tests of the PNG chunk walker (R-RES-3/4/5): the corpus' three chunk orders accepted, one negative
 *              per rule, the IDAT enumeration, the pixel budget, and an image at the limits inside the fuzz timeout
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <tl4/loader/png.h>
#include "pngbuild.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

static uint8_t img[8192];
static uint8_t big[TL4_PNG_MAX_RESOURCE + 64];
static tl4_png_info info;

static int cleared(const tl4_png_info *i)
{
    tl4_png_info z; memset(&z, 0, sizeof(z));
    return memcmp(i, &z, sizeof(z)) == 0;        /* the walker clears with memset: byte equality is the definition here */
}

/* corrupt one bit of the CRC of the chunk at `chunk_off` */
static void flip_crc(uint8_t *p, uint32_t chunk_off)
{
    uint32_t len = ((uint32_t)p[chunk_off] << 24) | ((uint32_t)p[chunk_off + 1] << 16) | ((uint32_t)p[chunk_off + 2] << 8) | p[chunk_off + 3];
    p[chunk_off + 8 + len + 3] ^= 1;
}

static void expect_reject(uint32_t n, tl4_status want, const char *what)
{
    tl4_status st = tl4_png_check(img, n, &info);
    checks++;
    if (st != want) { failures++; printf("FAIL %s: %s -> %s, wanted %s\n", __FILE__, what, tl4_status_name(st), tl4_status_name(want)); }
    CHECK(cleared(&info));
}

static void test_good(void)
{
    uint32_t n;
    /* the three corpus orders */
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 1);
    CHECK_STATUS(tl4_png_check(img, n, &info), TL4_OK);
    CHECK(info.width == 16 && info.height == 8 && info.pixels == 128 && info.colour_type == 6 && info.bit_depth == 8 && info.interlace == 0);
    CHECK(info.chunks == 6 && info.idat_chunks == 1 && info.idat_bytes == 100);
    CHECK(info.has_gama && info.has_phys && info.has_text && !info.has_srgb);
    n = pngb_image(img, sizeof(img), 3, 3720, 2, 300, 3, 1, 0);
    CHECK_STATUS(tl4_png_check(img, n, &info), TL4_OK);
    CHECK(info.has_srgb && !info.has_text && info.idat_chunks == 3 && info.idat_bytes == 300 && info.colour_type == 2 && info.pixels == 11160);
    n = pngb_image(img, sizeof(img), 1, 1, 6, 1, 1, 1, 1);
    CHECK_STATUS(tl4_png_check(img, n, &info), TL4_OK);
    CHECK(info.chunks == 7 && info.idat_bytes == 1 && info.pixels == 1);
    n = pngb_image(img, sizeof(img), 1, 1, 6, 0, 1, 1, 1);
    expect_reject(n, TL4_E_PNG_CHUNK, "no compressed data at all");
    /* the bare minimum: IHDR, IDAT, IEND */
    {
        uint32_t off = pngb_begin(img, sizeof(img));
        off = pngb_ihdr(img, sizeof(img), off, 8192, 1024, 8, 2, 0);
        off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 1);
        off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_OK);
        CHECK(info.pixels == TL4_PNG_MAX_PIXELS && info.chunks == 3);
    }
    /* determinism */
    {
        tl4_png_info again;
        n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 2, 1, 1);
        CHECK_STATUS(tl4_png_check(img, n, &info), TL4_OK);
        CHECK_STATUS(tl4_png_check(img, n, &again), TL4_OK);
        CHECK(memcmp(&info, &again, sizeof(info)) == 0);
    }
    CHECK_STATUS(tl4_png_check(img, n, 0), TL4_E_ARG);
    CHECK_STATUS(tl4_png_check(0, n, &info), TL4_E_ARG);
}

static void test_rejects(void)
{
    uint32_t n, off;
    uint8_t ihdr_bad[13];
    /* signature and size */
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 1);
    img[1] = 'Q'; expect_reject(n, TL4_E_PNG_SIGNATURE, "signature");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 1);
    expect_reject(40, TL4_E_PNG_SIGNATURE, "too short for a PNG");
    /* IHDR not first, wrong length, each parameter */
    off = pngb_begin(img, sizeof(img)); off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 4); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_IHDR, "IHDR not first");
    off = pngb_begin(img, sizeof(img)); memset(ihdr_bad, 0, 13); off = pngb_chunk(img, sizeof(img), off, "IHDR", ihdr_bad, 12);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_IHDR, "IHDR of 12 bytes");
    {
        struct { uint32_t w, h; uint8_t d, c, i; tl4_status want; const char *what; } cases[] = {
            { 4, 4, 16, 6, 0, TL4_E_PNG_IHDR, "bit depth 16" }, { 4, 4, 8, 3, 0, TL4_E_PNG_IHDR, "colour type 3" },
            { 4, 4, 8, 4, 0, TL4_E_PNG_IHDR, "colour type 4" }, { 4, 4, 8, 0, 0, TL4_E_PNG_IHDR, "greyscale" },
            { 4, 4, 8, 6, 1, TL4_E_PNG_IHDR, "interlaced" }, { 0, 4, 8, 6, 0, TL4_E_LIMIT, "width 0" },
            { 4, 0, 8, 6, 0, TL4_E_LIMIT, "height 0" }, { 8193, 1, 8, 6, 0, TL4_E_LIMIT, "side 8,193" },
            { 1, 8193, 8, 6, 0, TL4_E_LIMIT, "height 8,193" }, { 8192, 1025, 8, 6, 0, TL4_E_LIMIT, "over 8 M pixels" },
            { 0x80000000u, 1, 8, 6, 0, TL4_E_LIMIT, "width with the top bit" },
        };
        uint32_t k;
        for (k = 0; k < sizeof(cases) / sizeof(cases[0]); k++)
        {
            off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, cases[k].w, cases[k].h, cases[k].d, cases[k].c, cases[k].i);
            off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
            expect_reject(off, cases[k].want, cases[k].what);
        }
    }
    /* compression / filter bytes */
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0); img[8 + 8 + 10] = 1; pngb_put32(img + 8 + 8 + 13, pngb_crc(img + 8 + 4, 17));
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_IHDR, "compression 1");
    /* CRC off by one bit in each chunk kind */
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 1, 1);
    flip_crc(img, 8); expect_reject(n, TL4_E_PNG_CRC, "IHDR CRC");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 1, 1);
    flip_crc(img, 8 + 25); expect_reject(n, TL4_E_PNG_CRC, "sRGB CRC");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 1, 1);
    flip_crc(img, 8 + 25 + 13 + 16 + 21 + 27); expect_reject(n, TL4_E_PNG_CRC, "IDAT CRC");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 1, 1);
    flip_crc(img, n - 12); expect_reject(n, TL4_E_PNG_CRC, "IEND CRC");
    /* chunk structure */
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 0);
    expect_reject(n - 1, TL4_E_PNG_CHUNK, "last byte missing");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 0);
    img[n] = 0; expect_reject(n + 1, TL4_E_PNG_CHUNK, "trailing byte after IEND");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 0);
    expect_reject(n - 12, TL4_E_PNG_CHUNK, "IEND missing");
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 0);
    pngb_put32(img + 8 + 25 + 16 + 21, 0x80000000u); expect_reject(n, TL4_E_PNG_CHUNK, "length with the top bit");
    pngb_put32(img + 8 + 25 + 16 + 21, 100000u); expect_reject(n, TL4_E_PNG_CHUNK, "length past the end");
    /* the allow-list: repeats, order, lengths, shapes, unknown types */
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 4); off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 4);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "gAMA twice");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 4);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "gAMA between two IDAT");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "pHYs", 0, 9);
    off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "pHYs after IDAT");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 5); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "gAMA of 5 bytes");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "sRGB", 0, 2); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "sRGB of 2 bytes");
    {
        static const uint8_t no_nul[6] = { 'T', 'i', 't', 'l', 'e', 'x' };
        static const uint8_t two_nul[8] = { 'T', 0, 'a', 0, 'b', 'c', 'd', 'e' };
        static const uint8_t ctrl[8] = { 'T', 0, 'a', 7, 'b', 'c', 'd', 'e' };
        static uint8_t long_key[82];
        uint8_t tx[TL4_PNG_MAX_TEXT + 1];
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", no_nul, 6); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt without NUL");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", two_nul, 8); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt with two NUL");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", ctrl, 8); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt with a control byte");
        memset(long_key, 'k', 81); long_key[81] = 0;
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", long_key, 82); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt keyword of 81 characters");
        memset(tx, 'a', sizeof(tx)); tx[1] = 0;
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", tx, TL4_PNG_MAX_TEXT + 1); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt of 257 bytes");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", tx, TL4_PNG_MAX_TEXT); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_OK);                            /* 256 is the bound */
    }
    {
        const char *const unknown[] = { "iCCP", "cHRM", "bKGD", "tIME", "zTXt", "iTXt", "sBIT", "eXIf", "acTL", "prVt" };
        uint32_t k;
        for (k = 0; k < sizeof(unknown) / sizeof(unknown[0]); k++)
        {
            off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
            off = pngb_chunk(img, sizeof(img), off, unknown[k], 0, 4); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
            expect_reject(off, TL4_E_PNG_CHUNK, unknown[k]);
        }
    }
    /* PLTE and tRNS: rejected in every shape (no corpus evidence) */
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "PLTE", 0, 3); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "PLTE on colour type 6");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "tRNS", 0, 1); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "tRNS on colour type 6");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 2, 0);
    off = pngb_chunk(img, sizeof(img), off, "tRNS", 0, 5); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "tRNS of 5 bytes on colour type 2");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 2, 0);
    off = pngb_chunk(img, sizeof(img), off, "tRNS", 0, 6); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "tRNS of 6 bytes on colour type 2");
    /* the value ranges of the allow-list */
    {
        static const uint8_t zero4[4] = { 0, 0, 0, 0 };
        static const uint8_t unit2[9] = { 0, 0, 0x0B, 0x13, 0, 0, 0x0B, 0x13, 2 };
        static const uint8_t intent4[1] = { 4 };
        static const uint8_t empty_key[3] = { 0, 'a', 'b' };
        uint8_t key80[82];
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "gAMA", zero4, 4); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "gAMA 0");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "pHYs", unit2, 9); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "pHYs unit 2");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "sRGB", intent4, 1); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "sRGB 4");
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", empty_key, 3); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt with an empty keyword");
        memset(key80, 'k', 80); key80[80] = 0; key80[81] = 'a';
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", key80, 82); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        expect_reject(off, TL4_E_PNG_CHUNK, "tEXt keyword of 80 characters");
        memset(key80, 'k', 79); key80[79] = 0; key80[80] = 'a';
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        off = pngb_chunk(img, sizeof(img), off, "tEXt", key80, 81); off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_OK);                              /* 79 is the bound */
    }
    /* a zero-length chunk claimed past the end: the header fits, its CRC does not */
    n = pngb_image(img, sizeof(img), 16, 8, 6, 100, 1, 0, 0);
    memset(img + n, 0, 4); memcpy(img + n + 4, "IDAT", 4);
    expect_reject(n + 8, TL4_E_PNG_CHUNK, "zero-length chunk past the end");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 2, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "tRNS", 0, 6); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "tRNS after IDAT");
    /* IDAT and IEND rules */
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "gAMA", 0, 4); off = pngb_chunk(img, sizeof(img), off, "pHYs", 0, 9);
    off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "IEND without IDAT");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_SIGNATURE, "shorter than any PNG with an IDAT");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 1);
    expect_reject(off, TL4_E_PNG_CHUNK, "IEND with data");
    off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
    off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 8); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0); off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
    expect_reject(off, TL4_E_PNG_CHUNK, "a second IHDR");
    {
        uint32_t k;
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        for (k = 0; k < TL4_PNG_MAX_IDAT_CHUNKS; k++) off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 1);
        off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_E_LIMIT);        /* 66 chunks in all: the chunk bound */
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        for (k = 0; k < TL4_PNG_MAX_CHUNKS - 2; k++) off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 1);
        off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_OK); CHECK(info.chunks == TL4_PNG_MAX_CHUNKS && info.idat_chunks == TL4_PNG_MAX_CHUNKS - 2);
        /* 65 chunks: one IDAT more than the 64-chunk bound allows */
        off = pngb_begin(img, sizeof(img)); off = pngb_ihdr(img, sizeof(img), off, 4, 4, 8, 6, 0);
        for (k = 0; k < TL4_PNG_MAX_CHUNKS - 1; k++) off = pngb_chunk(img, sizeof(img), off, "IDAT", 0, 1);
        off = pngb_chunk(img, sizeof(img), off, "IEND", 0, 0);
        CHECK_STATUS(tl4_png_check(img, off, &info), TL4_E_LIMIT); CHECK(cleared(&info));
    }
}

static int enum_cb(uint32_t offset, uint32_t size, void *context)
{
    uint32_t *acc = (uint32_t *)context;
    acc[0]++; acc[1] += size; acc[2] = offset;
    return acc[0] == 2 && acc[3] ? 9 : 0;
}

static void test_enum_and_budget(void)
{
    uint32_t n, acc[4] = { 0, 0, 0, 0 };
    int stopped = -1;
    tl4_pixel_budget b = { 100, 0 };
    n = pngb_image(img, sizeof(img), 16, 8, 6, 300, 3, 1, 1);
    CHECK_STATUS(tl4_png_idat_enum(img, n, enum_cb, acc, &stopped), TL4_OK);
    CHECK(acc[0] == 3 && acc[1] == 300 && stopped == 0 && acc[2] + 100 + 4 + 12 == n);   /* the last IDAT ends before IEND */
    acc[0] = 0; acc[1] = 0; acc[3] = 1;
    CHECK_STATUS(tl4_png_idat_enum(img, n, enum_cb, acc, &stopped), TL4_OK);
    CHECK(acc[0] == 2 && stopped == 9);
    CHECK_STATUS(tl4_png_idat_enum(img, n, 0, acc, &stopped), TL4_E_ARG);
    /* a rejected image hands out no range: the last chunk's CRC, then a trailing byte, each with zero callbacks */
    img[n - 1] ^= 1;
    acc[0] = 0; acc[3] = 0;
    CHECK_STATUS(tl4_png_idat_enum(img, n, enum_cb, acc, &stopped), TL4_E_PNG_CRC); CHECK(acc[0] == 0);
    img[n - 1] ^= 1;
    img[n] = 0; acc[0] = 0;
    CHECK_STATUS(tl4_png_idat_enum(img, n + 1, enum_cb, acc, &stopped), TL4_E_PNG_CHUNK); CHECK(acc[0] == 0);
    acc[0] = 0;
    CHECK_STATUS(tl4_png_idat_enum(img, n, enum_cb, acc, &stopped), TL4_OK); CHECK(acc[0] == 3);
    CHECK_STATUS(tl4_png_idat_enum(img, n - 1, enum_cb, acc, &stopped), TL4_E_PNG_CHUNK);
    /* the budget */
    CHECK_STATUS(tl4_pixel_budget_take(&b, 60), TL4_OK); CHECK(b.used == 60);
    CHECK_STATUS(tl4_pixel_budget_take(&b, 41), TL4_E_LIMIT); CHECK(b.used == 60);
    CHECK_STATUS(tl4_pixel_budget_take(&b, 40), TL4_OK); CHECK(b.used == 100);
    CHECK_STATUS(tl4_pixel_budget_take(&b, 1), TL4_E_LIMIT);
    CHECK_STATUS(tl4_pixel_budget_take(&b, 0), TL4_OK);
    b.used = 101; CHECK_STATUS(tl4_pixel_budget_take(&b, 0), TL4_E_LIMIT);
    CHECK_STATUS(tl4_pixel_budget_take(0, 1), TL4_E_ARG);
    /* the limit: lowered as asked, never raised; an instance above the maximum never grants */
    CHECK_STATUS(tl4_pixel_budget_init(&b, 50), TL4_OK); CHECK(b.limit == 50 && b.used == 0);
    CHECK_STATUS(tl4_pixel_budget_init(&b, (uint64_t)TL4_PNG_MAX_BUDGET * 4), TL4_OK);
    CHECK(b.limit == TL4_PNG_MAX_BUDGET && b.used == 0);
    CHECK_STATUS(tl4_pixel_budget_take(&b, TL4_PNG_MAX_BUDGET), TL4_OK);
    CHECK_STATUS(tl4_pixel_budget_take(&b, 1), TL4_E_LIMIT);
    b.limit = (uint64_t)TL4_PNG_MAX_BUDGET + 1; b.used = 0;
    CHECK_STATUS(tl4_pixel_budget_take(&b, 1), TL4_E_LIMIT); CHECK(b.used == 0);
    CHECK_STATUS(tl4_pixel_budget_init(0, 1), TL4_E_ARG);
}

/* R-V4-14 for images: the largest image the limits allow walks inside the fuzz timeout */
static void test_limits(void)
{
    uint32_t n;
    clock_t t0, t1;
    double seconds;
#ifdef TL4_SANITIZED
    const double cap = 120.0;
#else
    const double cap = 5.0;
#endif
    /* 64 chunks in all: IHDR, sRGB, gAMA, pHYs, tEXt, 58 IDAT, IEND */
    n = pngb_image(big, sizeof(big), TL4_PNG_MAX_SIDE, TL4_PNG_MAX_PIXELS / TL4_PNG_MAX_SIDE, 6, TL4_PNG_MAX_COMPRESSED, TL4_PNG_MAX_CHUNKS - 6, 1, 1);
    CHECK(n > TL4_PNG_MAX_COMPRESSED);
    t0 = clock();
    CHECK_STATUS(tl4_png_check(big, n, &info), TL4_OK);
    t1 = clock();
    seconds = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("limits image: %u bytes, %u IDAT chunks, %llu pixels: %.3f s CPU (cap %.0f s)\n", n, info.idat_chunks,
           (unsigned long long)info.pixels, seconds, cap);
    CHECK(seconds < cap);
    CHECK(info.idat_bytes == TL4_PNG_MAX_COMPRESSED && info.pixels == TL4_PNG_MAX_PIXELS);
    /* one byte more of IDAT is over the compressed limit */
    n = pngb_image(big, sizeof(big), 4, 4, 6, TL4_PNG_MAX_COMPRESSED + 1, TL4_PNG_MAX_CHUNKS - 4, 0, 0);
    CHECK(n > 0);
    CHECK_STATUS(tl4_png_check(big, n, &info), TL4_E_LIMIT);
    CHECK(cleared(&info));
}

int main(void)
{
    test_good();
    test_rejects();
    test_enum_and_budget();
    test_limits();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
