/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the bounded string-table reader (R-RES-8; S2 plan)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Input layout (the query bytes sit in a fixed header so that a mutation of the image never shifts them):
 *   0..1  string id (little endian)       2..3  explicit language id       4  language mode: 0 = lowest, else explicit
 *   5..7  unused                          8..11 split: 0 = one image (the style); else the style ends there and the
 *                                               module (.mui) image follows, to the end of the input
 * Promises: no crash on any bytes; the check is deterministic and clears its statistics on a reject; when it passes,
 * the pair count equals tl4_pe_count_type(6); every located range lies inside the mapping with 1..1,024 units; get
 * agrees with locate (bytes, terminator), fails with TL4_E_BUFFER one unit short and never writes past the capacity;
 * the lowest-id lookup equals the explicit lookup of that id; get2 takes the style's answer unless it is "not found".
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/strings.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)
#define HEADER 12u

static uint16_t out[TL4_STR_MAX_CHARS + 2];
static uint16_t out2[TL4_STR_MAX_CHARS + 2];

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (rd16(p + 2) << 16); }

static int structural(tl4_status st) { return st == TL4_OK || st == TL4_E_NOT_FOUND || st == TL4_E_STR_BLOCK; }

/* Everything about one image and one query; returns the status of get for get2's promise. */
static tl4_status one_image(const tl4_pe *pe, uint32_t id, uint32_t language, uint32_t *got_chars)
{
    tl4_strings_stats s1, s2;
    tl4_status c1, c2, st, st2;
    uint32_t off = 0, n = 0, off2 = 0, n2 = 0, cnt = 0, i, got = 0;
    tl4_res_name type = tl4_res_id(6), name = tl4_res_id(id / 16u + 1u);
    tl4_resource block;

    c1 = tl4_strings_check(pe, &s1);
    c2 = tl4_strings_check(pe, &s2);
    PROMISE(c1 == c2 && memcmp(&s1, &s2, sizeof(s1)) == 0);                 /* deterministic */
    if (c1 != TL4_OK)
        PROMISE(s1.blocks == 0 && s1.entries == 0 && s1.strings == 0 && s1.max_chars == 0 && s1.longest_id == 0 && s1.max_languages == 0);
    else
    {
        PROMISE(tl4_pe_count_type(pe, &type, &cnt) == TL4_OK && cnt == s1.blocks);
        PROMISE(s1.entries >= s1.blocks && s1.max_languages <= TL4_PE_MAX_LANGUAGES && s1.max_chars <= TL4_STR_MAX_CHARS);
        PROMISE(s1.blocks <= TL4_STR_MAX_BLOCKS && s1.strings <= 16u * s1.entries);
    }

    st = tl4_strings_locate(pe, id, language, &off, &n);
    PROMISE(structural(st));
    if (st == TL4_OK)
    {
        PROMISE(n >= 1 && n <= TL4_STR_MAX_CHARS);
        PROMISE(off <= pe->size && 2u * (uint64_t)n <= pe->size - off);
        /* inside the block image of the entry that was asked for: the lowest one, or the one with that language */
        PROMISE((language == TL4_STR_LANG_LOWEST ? tl4_pe_find(pe, &type, &name, &block)
                                                 : tl4_pe_find_language(pe, &type, &name, language, &block)) == TL4_OK);
        PROMISE(off >= block.offset && off - block.offset + 2u * (uint64_t)n <= block.size);
        if (c1 == TL4_OK) PROMISE(s1.blocks > 0 && n <= s1.max_chars);
    }
    else
        PROMISE(off == 0 && n == 0);

    /* get agrees with locate; one unit short is TL4_E_BUFFER with a cleared first unit; a full buffer succeeds */
    out[TL4_STR_MAX_CHARS + 1] = 0xBEEF;
    st2 = tl4_strings_get(pe, id, language, out, n + 1u, &n2);
    if (st == TL4_OK)
    {
        PROMISE(st2 == TL4_OK || st2 == TL4_E_STR_BLOCK);                   /* a 0 unit is found on the copy only */
        if (st2 == TL4_OK)
        {
            got = n2;
            PROMISE(n2 == n && out[n] == 0);
            for (i = 0; i < n; i++) PROMISE(out[i] != 0 && out[i] == rd16(pe->base + off + 2u * i));
        }
        else
        {
            PROMISE(n2 == 0);
            for (i = 0; i <= n; i++) PROMISE(out[i] == 0);                   /* the whole copied range is cleared */
        }
        PROMISE(tl4_strings_get(pe, id, language, out2, n, &n2) == TL4_E_BUFFER && n2 == 0 && (n == 0 || out2[0] == 0));
    }
    else
        PROMISE(st2 == st && n2 == 0);
    PROMISE(out[TL4_STR_MAX_CHARS + 1] == 0xBEEF);
    PROMISE(tl4_strings_get(pe, id, language, out2, 0, &n2) == (st == TL4_OK ? TL4_E_BUFFER : st) && n2 == 0);

    /* the lowest-id lookup is the explicit lookup of the block's lowest id */
    if (language == TL4_STR_LANG_LOWEST && tl4_pe_find(pe, &type, &name, &block) == TL4_OK)
    {
        PROMISE(tl4_strings_locate(pe, id, block.language, &off2, &n2) == st && off2 == off && n2 == n);
    }
    *got_chars = got;
    return st2;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint32_t id, language, split, n_style = 0, n2 = 0;
    const uint8_t *body;
    size_t body_size, a_size, b_size;
    tl4_pe style, mui;
    tl4_strings pair;
    tl4_status st_style, st2;

    if (size < HEADER) return 0;
    id = rd16(data);
    language = data[4] ? rd16(data + 2) : TL4_STR_LANG_LOWEST;
    split = rd32(data + 8);
    body = data + HEADER;
    body_size = size - HEADER;
    a_size = split != 0 && split < body_size ? split : body_size;
    b_size = body_size - a_size;

    /* a NULL handle: no crash, cleared outputs */
    {
        uint32_t o = 7, c = 7;
        PROMISE(tl4_strings_locate(0, id, language, &o, &c) == TL4_E_ARG && o == 0 && c == 0);
    }

    if (tl4_pe_open(body, a_size, &style) != TL4_OK) return 0;
    st_style = one_image(&style, id, language, &n_style);

    /* a language outside LOWEST and 0..65,535 is an argument error on an open handle */
    {
        uint32_t o = 1, c = 1;
        PROMISE(tl4_strings_locate(&style, id, 0x10000u + (id & 0xFFu), &o, &c) == TL4_E_ARG && o == 0 && c == 0);
        PROMISE(tl4_strings_get(&style, id, 0x10000u, out2, 2, &c) == TL4_E_ARG && c == 0 && out2[0] == 0);
    }

    if (b_size != 0 && tl4_pe_open(body + a_size, b_size, &mui) == TL4_OK)
    {
        tl4_status st_mui, st_open = tl4_strings_open(&style, &mui, &pair);
        uint32_t n_mui = 0;
        st_mui = one_image(&mui, id, language, &n_mui);
        if (st_open != TL4_OK)
            PROMISE(pair.style == 0 && pair.mui == 0);
        else
        {
            PROMISE(pair.style == &style && pair.mui == &mui);
            memcpy(out2, out, sizeof(out2));                              /* out holds the mui's copy from one_image */
            st2 = tl4_strings_get2(&pair, id, language, out, TL4_STR_MAX_CHARS + 1u, &n2);
            if (st_style != TL4_E_NOT_FOUND)
            {
                PROMISE(st2 == st_style && n2 == n_style);                 /* the style's answer, whatever it is */
            }
            else
            {
                PROMISE(st2 == st_mui && n2 == n_mui);
                if (st2 == TL4_OK) PROMISE(memcmp(out, out2, 2u * (n2 + 1u)) == 0);
            }
        }
    }
    else
    {
        if (tl4_strings_open(&style, 0, &pair) == TL4_OK)
        {
            PROMISE(pair.style == &style && pair.mui == 0);
            st2 = tl4_strings_get2(&pair, id, language, out, TL4_STR_MAX_CHARS + 1u, &n2);
            PROMISE(st2 == st_style && n2 == n_style);
        }
        else
            PROMISE(pair.style == 0 && pair.mui == 0);
    }
    return 0;
}

#ifdef TL4_FUZZ_STANDALONE
#include <stdio.h>
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        FILE *f = fopen(argv[i], "rb");
        long len;
        uint8_t *buf;
        if (!f) { fprintf(stderr, "cannot open %s\n", argv[i]); return 2; }
        if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
        buf = (uint8_t *)malloc(len ? (size_t)len : 1);
        if (!buf) { fclose(f); return 2; }
        if (len && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 2; }
        fclose(f);
        LLVMFuzzerTestOneInput(buf, (size_t)len);
        free(buf);
    }
    printf("fuzz_strings: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
