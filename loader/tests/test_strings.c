/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The bounded string-table reader (R-RES-1, R-RES-2, R-RES-8; docs/design/L3-STRINGS-DESIGN.md section 4, 9.3;
 *              review MUSTs 1 to 5 of 2026-11-15)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Every input is generated in memory (R-PROC-1).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/strings.h>
#include <tl4/loader/v4.h>
#include "pebuild.h"
#include "v4build.h"
#include "v4fixture.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)
#define N(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

static uint8_t image[128 * 1024], image2[64 * 1024];
static uint8_t blk_a[TL4_STR_MAX_BLOCK + 64], blk_b[TL4_STR_MAX_BLOCK + 64], blk_c[TL4_STR_MAX_BLOCK + 64];
static const pb_options o32 = {0, 0, 0, 0};
static uint16_t out[TL4_STR_MAX_CHARS + 2];

static const char *const SIXTEEN[16] = {"s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12", "s13", "s14", "s15"};
static const char *const GERMAN[16] = {"de0", "de1", "de2", "de3", "de4", "de5", "de6", "de7", "de8", "de9", "de10", "de11", "de12", "de13", "de14", "de15"};
static const char *const ENGLISH[16] = {"en0", "en1", "en2", "en3", "en4", "en5", "en6", "en7", "en8", "en9", "en10", "en11", "en12", "en13", "en14", "en15"};
static const char *const HOLES[16] = {"h0", "h1", "h2", "", "h4", "h5", "h6", "h7", "h8", "h9", "h10", "h11", "h12", "h13", "h14", "h15"};
static const char *const MOD1[16] = {"m0", "m1", "m2", "m3", "m4", "m5", "m6", "m7", "m8", "m9", "m10", "m11", "m12", "m13", "m14", "m15"};
static const char *const MOD2[16] = {"m16", "m17", "m18", "m19", "m20", "m21", "m22", "m23", "m24", "m25", "m26", "m27", "m28", "m29", "m30", "m31"};

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* one type-6 resource in a PE32 image */
static size_t build1(uint32_t block_id, uint32_t language, const uint8_t *data, uint32_t size, uint8_t *dst, size_t cap)
{
    pb_res r;
    r.type_name = 0; r.type_id = 6; r.name_name = 0; r.name_id = block_id; r.language = language; r.data = data; r.size = size;
    return pb_build(&r, 1, &o32, dst, cap, 0);
}

/* a block whose slot i holds counts[i] copies of `unit` */
static uint32_t raw_block(uint8_t *dst, uint32_t cap, const uint32_t counts[16], uint32_t unit)
{
    uint32_t off = 0, i, u;
    for (i = 0; i < 16; i++)
    {
        if (off + 2 + 2 * counts[i] > cap) return 0;
        put16(dst + off, counts[i]); off += 2;
        for (u = 0; u < counts[i]; u++) { put16(dst + off, unit); off += 2; }
    }
    return off;
}

/* offset of the count of slot i inside a well-formed block image */
static uint32_t slot_off(const uint8_t *d, uint32_t i)
{
    uint32_t off = 0, k;
    for (k = 0; k < i; k++) off += 2 + 2 * ((uint32_t)d[off] | ((uint32_t)d[off + 1] << 8));
    return off;
}

static int eq_ascii(const uint16_t *s, uint32_t n, const char *a)
{
    size_t i, len = strlen(a);
    if (n != len) return 0;
    for (i = 0; i < len; i++) if (s[i] != (uint8_t)a[i]) return 0;
    return s[n] == 0;
}

static int stats_zero(const tl4_strings_stats *s)
{
    return s->blocks == 0 && s->entries == 0 && s->strings == 0 && s->max_chars == 0 && s->longest_id == 0 && s->max_languages == 0;
}

static void test_slots(void)
{
    uint32_t n = v4b_string_block(blk_a, sizeof(blk_a), SIXTEEN), off, chars, i;
    size_t size = build1(1, 1033, blk_a, n, image, sizeof(image));
    tl4_pe pe;
    tl4_strings_stats st;
    const uint8_t *s16;

    CHECK(size > 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == 1 && st.entries == 1 && st.strings == 16 && st.max_chars == 3 && st.longest_id == 10 && st.max_languages == 1);

    /* every slot of the block is addressable, by the lowest id and by the block's own language */
    for (i = 0; i < 16; i++)
    {
        CHECK_STATUS(tl4_strings_locate(&pe, i, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);
        CHECK(chars == strlen(SIXTEEN[i]) && off < size && off + 2 * chars <= size);
        CHECK_STATUS(tl4_strings_get(&pe, i, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK);
        CHECK(eq_ascii(out, chars, SIXTEEN[i]));
        CHECK_STATUS(tl4_strings_get(&pe, i, 1033, out, N(out), &chars), TL4_OK);
        CHECK(eq_ascii(out, chars, SIXTEEN[i]));
    }
    /* another language, another block, bad arguments: not found or argument, with the outputs cleared */
    off = chars = 7;
    CHECK_STATUS(tl4_strings_locate(&pe, 3, 1031, &off, &chars), TL4_E_NOT_FOUND); CHECK(off == 0 && chars == 0);
    off = chars = 7;
    CHECK_STATUS(tl4_strings_locate(&pe, 16, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_NOT_FOUND); CHECK(off == 0 && chars == 0);
    off = chars = 7;
    CHECK_STATUS(tl4_strings_locate(&pe, 0x10000u, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_ARG); CHECK(off == 0 && chars == 0);
    off = chars = 7;
    CHECK_STATUS(tl4_strings_locate(&pe, 3, 0x10000u, &off, &chars), TL4_E_ARG); CHECK(off == 0 && chars == 0);      /* MUST 5 */
    CHECK_STATUS(tl4_strings_locate(&pe, 3, 0xFFFFFFFEu, &off, &chars), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_locate(&pe, 3, TL4_STR_LANG_LOWEST, 0, &chars), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_locate(&pe, 3, TL4_STR_LANG_LOWEST, &off, 0), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_locate(0, 3, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_ARG);

    /* the getter's buffer rule (MUST 4): needed capacity chars + 1, nothing truncated, first unit cleared */
    chars = 7; out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get(&pe, 10, TL4_STR_LANG_LOWEST, out, 0, &chars), TL4_E_BUFFER); CHECK(chars == 0 && out[0] == 0x41);
    out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get(&pe, 10, TL4_STR_LANG_LOWEST, out, 3, &chars), TL4_E_BUFFER); CHECK(chars == 0 && out[0] == 0);
    CHECK_STATUS(tl4_strings_get(&pe, 10, TL4_STR_LANG_LOWEST, out, 4, &chars), TL4_OK); CHECK(chars == 3 && eq_ascii(out, 3, "s10"));
    CHECK_STATUS(tl4_strings_get(&pe, 10, TL4_STR_LANG_LOWEST, 0, 4, &chars), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_get(&pe, 10, TL4_STR_LANG_LOWEST, out, 4, 0), TL4_E_ARG);
    out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get(&pe, 16, TL4_STR_LANG_LOWEST, out, 4, &chars), TL4_E_NOT_FOUND); CHECK(chars == 0 && out[0] == 0);
    out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get(&pe, 3, 0x10000u, out, 4, &chars), TL4_E_ARG); CHECK(chars == 0 && out[0] == 0);

    /* the L1 entry point is the lowest-id lookup */
    CHECK_STATUS(tl4_v4_find_string(&pe, 0, &s16, &chars), TL4_OK);
    CHECK_STATUS(tl4_strings_locate(&pe, 0, TL4_STR_LANG_LOWEST, &off, &i), TL4_OK);
    CHECK(chars == 2 && s16 == pe.base + off && i == 2);
    CHECK_STATUS(tl4_v4_find_string(&pe, 16, &s16, &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_v4_find_string(&pe, 0x10000u, &s16, &chars), TL4_E_ARG);
}

static void test_empty_and_absent(void)
{
    uint32_t n = v4b_string_block(blk_a, sizeof(blk_a), HOLES), off, chars;
    size_t size = build1(1, 0, blk_a, n, image, sizeof(image));
    tl4_pe pe;
    tl4_strings_stats st;
    pb_res img;

    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.strings == 15 && st.blocks == 1);
    CHECK_STATUS(tl4_strings_locate(&pe, 3, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_NOT_FOUND);  /* empty slot */
    CHECK_STATUS(tl4_strings_locate(&pe, 3, 0, &off, &chars), TL4_E_NOT_FOUND);                    /* language 0 is a valid explicit id */
    CHECK_STATUS(tl4_strings_locate(&pe, 4, 0, &off, &chars), TL4_OK);

    /* a file without a string table passes with zero counts; a lookup is "not found" */
    img.type_name = "IMAGE"; img.type_id = 0; img.name_name = 0; img.name_id = 1; img.language = 0; img.data = blk_a; img.size = 16;
    size = build1(1, 0, blk_a, n, image, sizeof(image));
    size = pb_build(&img, 1, &o32, image, sizeof(image), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    st.blocks = 5;
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(stats_zero(&st));
    CHECK_STATUS(tl4_strings_locate(&pe, 0, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_check(0, &st), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_check(&pe, 0), TL4_E_ARG);
}

static void test_shape(void)
{
    uint32_t n = v4b_string_block(blk_a, sizeof(blk_a), SIXTEEN), m, off, chars;
    uint32_t counts[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    size_t size;
    tl4_pe pe;
    tl4_strings_stats st;
    pb_res named;
    const uint8_t *s16;

    /* one byte short: the last string runs past the end */
    size = build1(1, 1033, blk_a, n - 1, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    st.blocks = 5;
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    CHECK(stats_zero(&st));
    /* one byte long: the 16 strings do not end on the size */
    memcpy(blk_b, blk_a, n); blk_b[n] = 0;
    size = build1(1, 1033, blk_b, n + 1, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    /* a count inside the limit but past the end of the block */
    memcpy(blk_b, blk_a, n); put16(blk_b + slot_off(blk_b, 15), 1000);
    size = build1(1, 1033, blk_b, n, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    CHECK_STATUS(tl4_strings_locate(&pe, 15, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_STR_BLOCK);
    CHECK_STATUS(tl4_strings_locate(&pe, 14, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);   /* the strings before it are intact */
    /* a code unit 0 inside a string (MUST 1) */
    memcpy(blk_b, blk_a, n); put16(blk_b + slot_off(blk_b, 7) + 2, 0);
    size = build1(1, 1033, blk_b, n, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);

    /* a string of exactly 1,024 code units passes; 1,025 does not */
    counts[0] = TL4_STR_MAX_CHARS;
    m = raw_block(blk_c, sizeof(blk_c), counts, 'a');
    CHECK(m == 2 * 16 + 2 * TL4_STR_MAX_CHARS);
    size = build1(2, 1033, blk_c, m, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.max_chars == TL4_STR_MAX_CHARS && st.longest_id == 16 && st.strings == 1);
    CHECK_STATUS(tl4_strings_get(&pe, 16, TL4_STR_LANG_LOWEST, out, TL4_STR_MAX_CHARS, &chars), TL4_E_BUFFER);
    CHECK_STATUS(tl4_strings_get(&pe, 16, TL4_STR_LANG_LOWEST, out, TL4_STR_MAX_CHARS + 1, &chars), TL4_OK);
    CHECK(chars == TL4_STR_MAX_CHARS && out[0] == 'a' && out[TL4_STR_MAX_CHARS - 1] == 'a' && out[TL4_STR_MAX_CHARS] == 0);
    CHECK_STATUS(tl4_v4_find_string(&pe, 16, &s16, &chars), TL4_E_NOT_FOUND);                 /* R-RES-2: a referenced string is at most 255 */
    counts[0] = TL4_STR_MAX_CHARS + 1;
    m = raw_block(blk_c, sizeof(blk_c), counts, 'a');
    size = build1(2, 1033, blk_c, m, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    CHECK_STATUS(tl4_strings_locate(&pe, 16, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_STR_BLOCK);
    CHECK_STATUS(tl4_strings_locate(&pe, 17, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_STR_BLOCK);  /* a bad count before the wanted one */
    /* 255 and 256 at the L1 entry point */
    counts[0] = 255;
    m = raw_block(blk_c, sizeof(blk_c), counts, 'b');
    size = build1(2, 1033, blk_c, m, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_find_string(&pe, 16, &s16, &chars), TL4_OK); CHECK(chars == 255);
    counts[0] = 256;
    m = raw_block(blk_c, sizeof(blk_c), counts, 'b');
    size = build1(2, 1033, blk_c, m, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_find_string(&pe, 16, &s16, &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_locate(&pe, 16, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK); CHECK(chars == 256);

    /* block ids: 4,096 is the last one, 4,097 and 0 are rejects, so is a named block */
    size = build1(TL4_STR_MAX_BLOCKS, 1033, blk_a, n, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == 1 && st.longest_id == 16 * (TL4_STR_MAX_BLOCKS - 1) + 10);
    CHECK_STATUS(tl4_strings_get(&pe, 0xFFFFu, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "s15"));
    size = build1(TL4_STR_MAX_BLOCKS + 1, 1033, blk_a, n, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    size = build1(0, 1033, blk_a, n, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    named.type_name = 0; named.type_id = 6; named.name_name = "STRINGS"; named.name_id = 0; named.language = 1033; named.data = blk_a; named.size = n;
    size = pb_build(&named, 1, &o32, image, sizeof(image), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
}

static void test_languages(void)
{
    uint32_t na = v4b_string_block(blk_a, sizeof(blk_a), GERMAN), nb = v4b_string_block(blk_b, sizeof(blk_b), ENGLISH);
    uint32_t off, chars, off2, chars2, i;
    pb_res two[2], many[TL4_PE_MAX_LANGUAGES];
    tl4_res_name type = tl4_res_id(6), name = tl4_res_id(1), other = tl4_res_id(9);
    tl4_resource res;
    size_t size;
    tl4_pe pe;
    tl4_strings_stats st;
    tl4_v4_stats vs;
    v4f_parts parts;

    two[0].type_name = 0; two[0].type_id = 6; two[0].name_name = 0; two[0].name_id = 1; two[0].language = 1033; two[0].data = blk_b; two[0].size = nb;
    two[1] = two[0]; two[1].language = 1031; two[1].data = blk_a; two[1].size = na;
    size = pb_build(two, 2, &o32, image, sizeof(image), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK(pe.nmulti_pairs == 1);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == 1 && st.entries == 2 && st.strings == 32 && st.max_languages == 2 && st.max_chars == 4);
    CHECK_STATUS(tl4_strings_get(&pe, 5, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "de5"));
    CHECK_STATUS(tl4_strings_get(&pe, 5, 1031, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "de5"));
    CHECK_STATUS(tl4_strings_get(&pe, 5, 1033, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "en5"));
    CHECK_STATUS(tl4_strings_get(&pe, 5, 1034, out, N(out), &chars), TL4_E_NOT_FOUND); CHECK(chars == 0 && out[0] == 0);
    CHECK_STATUS(tl4_strings_get(&pe, 5, 1032, out, N(out), &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_get(&pe, 5, 0, out, N(out), &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_locate(&pe, 5, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);
    CHECK_STATUS(tl4_strings_locate(&pe, 5, 1031, &off2, &chars2), TL4_OK);
    CHECK(off == off2 && chars == chars2);
    /* the reader's explicit-language lookup */
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 1033, &res), TL4_OK); CHECK(res.language == 1033 && res.nlanguages == 2 && res.size == nb);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 1031, &res), TL4_OK); CHECK(res.language == 1031 && res.size == na);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 1032, &res), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 0, &res), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 2000, &res), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &other, 1033, &res), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 0x10000u, &res), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, TL4_STR_LANG_LOWEST, &res), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 1033, 0), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_find(&pe, &type, &name, &res), TL4_OK); CHECK(res.language == 1031);
    /* the second language malformed rejects the file; the first alone passes */
    two[0].size = nb - 1;
    size = pb_build(two, 2, &o32, image, sizeof(image), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_E_STR_BLOCK);
    CHECK(stats_zero(&st));
    size = pb_build(two + 1, 1, &o32, image, sizeof(image), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);

    /* 64 languages on one block */
    for (i = 0; i < TL4_PE_MAX_LANGUAGES; i++) { many[i] = two[1]; many[i].language = i; }
    size = pb_build(many, TL4_PE_MAX_LANGUAGES, &o32, image, sizeof(image), 0);
    CHECK(size > 0);
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == 1 && st.entries == 64 && st.strings == 16 * 64 && st.max_languages == 64);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 63, &res), TL4_OK); CHECK(res.language == 63);
    CHECK_STATUS(tl4_pe_find_language(&pe, &type, &name, 64, &res), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_locate(&pe, 7, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);
    CHECK_STATUS(tl4_strings_locate(&pe, 7, 0, &off2, &chars2), TL4_OK);
    CHECK(off == off2 && chars == chars2);
    CHECK_STATUS(tl4_strings_get(&pe, 7, 63, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "de7"));

    /* the version-4 validator applies the whole-table check (R-RES-8) */
    v4f_default(&parts);
    size = v4f_build(&parts, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &vs), TL4_OK);
    parts.strings_size -= 1;
    size = v4f_build(&parts, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &vs), TL4_E_STR_BLOCK);
    CHECK(vs.cmap_names == 0 && vs.string_blocks == 0);
    parts.strings_size += 1;
    put16(parts.strings + slot_off(parts.strings, 5) + 2, 0);                                /* a code unit 0 in the font string */
    size = v4f_build(&parts, image, sizeof(image));
    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &vs), TL4_E_STR_BLOCK);
}

/* R-GEN-11, review MUST 2: the bytes change after the check; the lookup bounds everything again */
static void test_changed_bytes(void)
{
    uint32_t n = v4b_string_block(blk_a, sizeof(blk_a), SIXTEEN), off, chars, base;
    size_t size = build1(1, 1033, blk_a, n, image, sizeof(image));
    tl4_pe pe;
    tl4_strings_stats st;
    tl4_res_name type = tl4_res_id(6), name = tl4_res_id(1);
    tl4_resource block;

    CHECK_STATUS(tl4_pe_open(image, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK_STATUS(tl4_pe_find(&pe, &type, &name, &block), TL4_OK);
    base = block.offset;
    CHECK(memcmp(image + base, blk_a, n) == 0);

    /* a count BEFORE the wanted string, past the end: reject, not a wrong string */
    put16(image + base + slot_off(blk_a, 0), 0x7FFF);
    off = chars = 7;
    CHECK_STATUS(tl4_strings_locate(&pe, 5, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_STR_BLOCK); CHECK(off == 0 && chars == 0);
    out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get(&pe, 5, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_STR_BLOCK); CHECK(chars == 0 && out[0] == 0);
    memcpy(image + base, blk_a, n);
    /* a count before it changed to another value inside the block: the parse shifts onto text bytes, and the only
     * outcomes are a reject or a range that lies inside the block (R-GEN-11) */
    for (chars = 0; chars <= 3; chars++)
    {
        tl4_status rc;
        uint32_t o = 0, c = 0;
        put16(image + base + slot_off(blk_a, 0), chars);
        rc = tl4_strings_locate(&pe, 5, TL4_STR_LANG_LOWEST, &o, &c);
        CHECK(rc == TL4_E_STR_BLOCK || rc == TL4_E_NOT_FOUND || (rc == TL4_OK && o >= base && o + 2 * c <= base + n));
        if (rc != TL4_OK) CHECK(o == 0 && c == 0);
    }
    memcpy(image + base, blk_a, n);
    /* the wanted string's own count past the end */
    put16(image + base + slot_off(blk_a, 5), 0x4000);
    CHECK_STATUS(tl4_strings_locate(&pe, 5, TL4_STR_LANG_LOWEST, &off, &chars), TL4_E_STR_BLOCK);
    CHECK_STATUS(tl4_strings_locate(&pe, 4, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);
    memcpy(image + base, blk_a, n);
    /* a code unit 0 written into the wanted string after the check: the range is fine, the copy is refused (MUST 1) */
    put16(image + base + slot_off(blk_a, 5) + 4, 0);                                       /* "s5": the second unit */
    CHECK_STATUS(tl4_strings_locate(&pe, 5, TL4_STR_LANG_LOWEST, &off, &chars), TL4_OK);
    memset(out, 0x41, 8);
    CHECK_STATUS(tl4_strings_get(&pe, 5, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_STR_BLOCK);
    CHECK(chars == 0 && out[0] == 0 && out[1] == 0 && out[2] == 0 && out[3] == 0x4141);    /* the whole copied range, nothing beyond */
    memcpy(image + base, blk_a, n);
    CHECK_STATUS(tl4_strings_get(&pe, 5, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "s5"));
}

/* the style / module pair (R-RES-1; review MUST 3) */
static void test_pair(void)
{
    uint32_t ns = v4b_string_block(blk_a, sizeof(blk_a), HOLES), n1 = v4b_string_block(blk_b, sizeof(blk_b), MOD1),
             n2 = v4b_string_block(blk_c, sizeof(blk_c), MOD2), chars, base;
    pb_res mod[2];
    size_t size, size2;
    tl4_pe style, mui;
    tl4_strings pair;
    tl4_res_name type = tl4_res_id(6), name = tl4_res_id(1);
    tl4_resource block;

    size = build1(1, 0, blk_a, ns, image, sizeof(image));
    mod[0].type_name = 0; mod[0].type_id = 6; mod[0].name_name = 0; mod[0].name_id = 1; mod[0].language = 1033; mod[0].data = blk_b; mod[0].size = n1;
    mod[1] = mod[0]; mod[1].name_id = 2; mod[1].data = blk_c; mod[1].size = n2;
    size2 = pb_build(mod, 2, &o32, image2, sizeof(image2), 0);
    CHECK_STATUS(tl4_pe_open(image, size, &style), TL4_OK);
    CHECK_STATUS(tl4_pe_open(image2, size2, &mui), TL4_OK);

    pair.style = pair.mui = (const tl4_pe *)1;
    CHECK_STATUS(tl4_strings_open(&style, &mui, &pair), TL4_OK);
    CHECK(pair.style == &style && pair.mui == &mui);
    CHECK_STATUS(tl4_strings_get2(&pair, 0, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "h0"));    /* the style first */
    CHECK_STATUS(tl4_strings_get2(&pair, 3, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "m3"));    /* empty slot: the module */
    CHECK_STATUS(tl4_strings_get2(&pair, 20, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "m20"));  /* absent block: the module */
    CHECK_STATUS(tl4_strings_get2(&pair, 40, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_NOT_FOUND); CHECK(chars == 0 && out[0] == 0);
    CHECK_STATUS(tl4_strings_get2(&pair, 0, 1033, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "m0"));                   /* the style has no 1033 entry */
    CHECK_STATUS(tl4_strings_get2(&pair, 0, 0, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "h0"));
    CHECK_STATUS(tl4_strings_get2(&pair, 0, 0x10000u, out, N(out), &chars), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_get2(&pair, 0, TL4_STR_LANG_LOWEST, out, 2, &chars), TL4_E_BUFFER);                                      /* "h0" needs 3 */
    CHECK_STATUS(tl4_strings_get2(0, 0, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_ARG);
    CHECK_STATUS(tl4_strings_get2(&pair, 0, TL4_STR_LANG_LOWEST, 0, N(out), &chars), TL4_E_ARG);

    /* MUST 3: only "not found" in the style goes on to the module; a malformed style block is returned as it is */
    CHECK_STATUS(tl4_pe_find(&style, &type, &name, &block), TL4_OK);
    base = block.offset;
    put16(image + base + slot_off(blk_a, 0), 0x7FFF);
    out[0] = 0x41;
    CHECK_STATUS(tl4_strings_get2(&pair, 3, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_STR_BLOCK); CHECK(chars == 0 && out[0] == 0);
    memcpy(image + base, blk_a, ns);
    CHECK_STATUS(tl4_strings_get2(&pair, 3, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK);

    /* a malformed module fails the open and clears the pair; no module: "not found" stays */
    mod[1].size = n2 - 1;
    size2 = pb_build(mod, 2, &o32, image2, sizeof(image2), 0);
    CHECK_STATUS(tl4_pe_open(image2, size2, &mui), TL4_OK);
    pair.style = pair.mui = (const tl4_pe *)1;
    CHECK_STATUS(tl4_strings_open(&style, &mui, &pair), TL4_E_STR_BLOCK);
    CHECK(pair.style == 0 && pair.mui == 0);
    CHECK_STATUS(tl4_strings_open(&style, 0, &pair), TL4_OK);
    CHECK(pair.style == &style && pair.mui == 0);
    CHECK_STATUS(tl4_strings_get2(&pair, 3, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_strings_get2(&pair, 0, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK);
    CHECK_STATUS(tl4_strings_open(0, 0, &pair), TL4_E_ARG);
    CHECK(pair.style == 0 && pair.mui == 0);
    CHECK_STATUS(tl4_strings_open(&style, 0, 0), TL4_E_ARG);
}

/* section 6: 4,096 blocks, and the largest table a file the reader accepts can hold (review SHOULD) */
static void test_limits(void)
{
    static pb_res many[TL4_STR_MAX_BLOCKS];
    uint32_t n = v4b_string_block(blk_a, sizeof(blk_a), SIXTEEN), i, m, chars, nblocks;
    uint32_t counts[16] = {TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS,
                          TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS, TL4_STR_MAX_CHARS};
    uint8_t *big = (uint8_t *)malloc(TL4_PE_MAX_FILE_SIZE);
    size_t size;
    tl4_pe pe;
    tl4_strings_stats st;

    if (!big) { printf("test_limits: no memory, skipped\n"); return; }
    for (i = 0; i < TL4_STR_MAX_BLOCKS; i++)
    {
        many[i].type_name = 0; many[i].type_id = 6; many[i].name_name = 0; many[i].name_id = i + 1; many[i].language = 1033;
        many[i].data = blk_a; many[i].size = n;
    }
    size = pb_build(many, TL4_STR_MAX_BLOCKS, &o32, big, TL4_PE_MAX_FILE_SIZE, 0);
    CHECK(size > 0);
    CHECK_STATUS(tl4_pe_open(big, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == TL4_STR_MAX_BLOCKS && st.entries == TL4_STR_MAX_BLOCKS && st.strings == 16 * TL4_STR_MAX_BLOCKS);
    CHECK_STATUS(tl4_strings_get(&pe, 0xFFFFu, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "s15"));
    CHECK_STATUS(tl4_strings_get(&pe, 0, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK); CHECK(eq_ascii(out, chars, "s0"));

    /* full blocks of 32,800 bytes, as many as a 32 MiB file holds */
    m = raw_block(blk_c, sizeof(blk_c), counts, 'z');
    CHECK(m == TL4_STR_MAX_BLOCK);
    for (i = 0; i < TL4_STR_MAX_BLOCKS; i++) { many[i].data = blk_c; many[i].size = m; }
    for (nblocks = TL4_PE_MAX_FILE_SIZE / TL4_STR_MAX_BLOCK; nblocks > 0; nblocks--)
        if ((size = pb_build(many, nblocks, &o32, big, TL4_PE_MAX_FILE_SIZE, 0)) != 0) break;
    CHECK(nblocks >= 1000 && size <= TL4_PE_MAX_FILE_SIZE);
    CHECK_STATUS(tl4_pe_open(big, size, &pe), TL4_OK);
    CHECK_STATUS(tl4_strings_check(&pe, &st), TL4_OK);
    CHECK(st.blocks == nblocks && st.strings == 16 * nblocks && st.max_chars == TL4_STR_MAX_CHARS && st.longest_id == 0);
    CHECK_STATUS(tl4_strings_get(&pe, 16 * nblocks - 1, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_OK);
    CHECK(chars == TL4_STR_MAX_CHARS && out[0] == 'z' && out[TL4_STR_MAX_CHARS] == 0);
    CHECK_STATUS(tl4_strings_get(&pe, 16 * nblocks, TL4_STR_LANG_LOWEST, out, N(out), &chars), TL4_E_NOT_FOUND);
    printf("largest table in a %u MiB file: %u blocks of %u bytes, %lu bytes in all\n", TL4_PE_MAX_FILE_SIZE >> 20, nblocks, m, (unsigned long)size);
    free(big);
}

int main(void)
{
    test_slots();
    test_empty_and_absent();
    test_shape();
    test_languages();
    test_changed_bytes();
    test_pair();
    test_limits();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
