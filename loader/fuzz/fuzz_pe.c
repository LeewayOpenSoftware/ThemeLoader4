/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the bounded PE / resource reader (R-PE-11)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Built with -fsanitize=fuzzer by clang (TL4_FUZZ=ON), or as a standalone file driver (TL4_FUZZ_STANDALONE) for
 * compilers without libFuzzer, which runs every file given on the command line through the same entry point.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/pe.h>

/* A broken promise of the reader is a crash the fuzzer reports, not something ASan has to notice by luck. */
#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

struct touch_ctx {
    const tl4_pe *pe;
    uint32_t seen;
    int changed;        /* the buffer was overwritten after the open: ranges still hold, agreement need not */
};

static void check_name(const tl4_pe *pe, const tl4_res_name *n)
{
    if (n->kind == TL4_NAME_UTF16)
    {
        PROMISE(n->length >= 1 && n->length <= TL4_PE_MAX_NAME_CHARS);
        PROMISE(n->utf16 >= pe->base && (size_t)(n->utf16 - pe->base) + 2u * n->length <= pe->size);
    }
    else
        PROMISE(n->kind == TL4_NAME_ID && n->id <= 0xFFFFu);
}

static int touch_cb(const tl4_resource *r, void *ctx)
{
    struct touch_ctx *c = (struct touch_ctx *)ctx;
    const tl4_pe *pe = c->pe;
    tl4_resource again;
    volatile uint8_t sink = 0;

    /* every range the reader hands out lies inside the mapped input (R-PE-9) */
    PROMISE(r->offset <= pe->size && r->size <= pe->size - r->offset);
    PROMISE(r->data == pe->base + r->offset);
    PROMISE(r->language <= 0xFFFFu);
    check_name(pe, &r->type);
    check_name(pe, &r->name);
    if (r->size)
    {
        sink = r->data[0];
        sink = r->data[r->size - 1];
    }
    (void)sink;
    /* find() agrees with enum(): the same (type, name) resolves to the same bytes (on an unchanged buffer) */
    if (!c->changed)
    {
        PROMISE(tl4_pe_find(pe, &r->type, &r->name, &again) == TL4_OK);
        PROMISE(again.offset == r->offset && again.size == r->size && again.language == r->language);
    }
    c->seen++;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_pe pe, pe2;
    tl4_resource r;
    tl4_res_name types[] = {tl4_res_ascii("IMAGE"), tl4_res_ascii("VARIANT"), tl4_res_ascii("CMAP"),
                            tl4_res_ascii("PACKTHEM_VERSION"), tl4_res_id(6)};
    tl4_res_name names[] = {tl4_res_id(1), tl4_res_id(508), tl4_res_ascii("NORMAL"), tl4_res_ascii("CMAP")};
    size_t i, j;
    int stopped = 0;
    struct touch_ctx ctx;
    uint32_t cnt;
    tl4_status st, st2;
    /* S1 point 6: an exact-size private copy, so an over-read by one byte faults */
    uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
    if (!copy) return 0;
    memcpy(copy, data, size);

    st = tl4_pe_open(copy, size, &pe);
    /* S1 point 5: the same bytes give the same status and the same handle, field by field */
    st2 = tl4_pe_open(copy, size, &pe2);
    PROMISE(st == st2 && memcmp(&pe, &pe2, sizeof(pe)) == 0);
    if (st != TL4_OK)
    {
        PROMISE(pe.base == NULL);  /* R-GEN-5 */
        free(copy);
        return 0;
    }
    ctx.pe = &pe;
    ctx.seen = 0;
    ctx.changed = 0;
    PROMISE(tl4_pe_enum(&pe, touch_cb, &ctx, &stopped) == TL4_OK && stopped == 0);
    PROMISE(ctx.seen == pe.nresources);
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        PROMISE(tl4_pe_count_type(&pe, &types[i], &cnt) == TL4_OK);
        for (j = 0; j < sizeof(names) / sizeof(names[0]); j++)
            if (tl4_pe_find(&pe, &types[i], &names[j], &r) == TL4_OK) touch_cb(&r, &ctx);
    }

    /* S1 section 4 (R-GEN-11): the input changes after the validating open. The second half of the input, repeated,
     * overwrites the buffer; every lookup must then reject or hand out ranges that are still inside the buffer. */
    if (size >= 2)
    {
        size_t half = size / 2;
        for (i = 0; i < size; i++) copy[i] = data[half + (i % (size - half))];
        ctx.seen = 0;
        ctx.changed = 1;
        (void)tl4_pe_enum(&pe, touch_cb, &ctx, &stopped);        /* touch_cb asserts the ranges */
        for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
        {
            (void)tl4_pe_count_type(&pe, &types[i], &cnt);
            for (j = 0; j < sizeof(names) / sizeof(names[0]); j++)
                if (tl4_pe_find(&pe, &types[i], &names[j], &r) == TL4_OK)
                    PROMISE(r.offset <= pe.size && r.size <= pe.size - r.offset && r.data == pe.base + r.offset);
        }
    }
    free(copy);
    return 0;
}

#ifdef TL4_FUZZ_STANDALONE
#include <stdio.h>
#include <stdlib.h>

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
    printf("fuzz_pe: %d inputs, no crash\n", argc - 1);
    return 0;
}
#endif
