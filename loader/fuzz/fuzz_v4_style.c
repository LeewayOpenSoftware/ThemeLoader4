/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness: a PE image through tl4_pe_open and tl4_v4_validate (S1 plan: fuzz_v4_style)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/v4.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

/* Statistics are compared field by field, never as raw struct bytes (padding is not data). */
static int walk_equal(const tl4_v4_walk_stats *a, const tl4_v4_walk_stats *b)
{
    return a->records == b->records && a->dropped_unknown_type == b->dropped_unknown_type &&
           a->dropped_ref_on_value == b->dropped_ref_on_value && a->partstates == b->partstates &&
           a->classes == b->classes && a->max_part == b->max_part && a->max_state == b->max_state && a->max_cb == b->max_cb;
}

static int stats_equal(const tl4_v4_stats *a, const tl4_v4_stats *b)
{
    return a->cmap_names == b->cmap_names && a->descriptors == b->descriptors && a->bcmap_entries == b->bcmap_entries &&
           a->bcmap_max_depth == b->bcmap_max_depth && walk_equal(&a->variant, &b->variant) && walk_equal(&a->rmap, &b->rmap) &&
           a->images == b->images && a->streams == b->streams && a->string_blocks == b->string_blocks &&
           a->fonts == b->fonts && a->filenames == b->filenames && a->diskstreams == b->diskstreams && a->strings == b->strings &&
           a->unresolved_fonts == b->unresolved_fonts && a->unresolved_filenames == b->unresolved_filenames &&
           a->unresolved_diskstreams == b->unresolved_diskstreams && a->unresolved_strings == b->unresolved_strings;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_pe pe;
    tl4_v4_stats s, s2;
    tl4_status st, st2;
    uint8_t *copy = (uint8_t *)malloc(size ? size : 1);   /* exact-size copy: an over-read faults (S1 point 6) */
    if (!copy) return 0;
    memcpy(copy, data, size);

    if (tl4_pe_open(copy, size, &pe) != TL4_OK) { PROMISE(pe.base == NULL); free(copy); return 0; }
    st = tl4_v4_validate(&pe, 0, &s);
    st2 = tl4_v4_validate(&pe, 0, &s2);
    PROMISE(st == st2 && stats_equal(&s, &s2));                                  /* R-GEN-8 determinism */
    if (st == TL4_OK)
    {
        PROMISE(s.cmap_names >= 1 && s.cmap_names <= TL4_V4_MAX_CLASSES);
        PROMISE(s.descriptors <= TL4_V4_MAX_DESCRIPTORS && s.bcmap_entries + s.descriptors == s.cmap_names);
        PROMISE(s.bcmap_max_depth <= TL4_V4_MAX_CHAIN);
        PROMISE(s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value <= TL4_V4_MAX_RECORDS);
        PROMISE(s.variant.partstates <= TL4_V4_MAX_PARTSTATES && s.variant.classes <= TL4_V4_MAX_CLASSES);
        PROMISE(s.images <= TL4_V4_MAX_IMAGES && s.streams <= TL4_V4_MAX_STREAMS);
        PROMISE(s.unresolved_fonts == 0);
        PROMISE(s.unresolved_filenames <= s.filenames && s.unresolved_diskstreams <= s.diskstreams);
    }
    else
    {
        tl4_v4_stats zero;
        memset(&zero, 0, sizeof(zero));
        PROMISE(stats_equal(&s, &zero));                                          /* R-GEN-5 */
    }
    free(copy);
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
    printf("fuzz_v4_style: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
