/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness: the style changes inside tl4_model_build, between its validation and its second
 *              read of the maps and records (R-GEN-11, R-MOD-5; S1 plan section 4 applied to the model)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The input is split into two images A and B of the same length. The build runs on a buffer holding A; the
 * between-passes hook overwrites it with B. The only acceptable outcomes are a clean reject with a cleared model, or
 * a model whose arrays stay within the buffer A sized and whose ranges lie inside the mapping; when B validates on its
 * own, the model must equal the one B gives on its own (field by field). Built against tl4_loader_core_testing
 * (TL4_TESTING): the shipped core has no hook.
 */
#ifndef TL4_TESTING
#error "fuzz_v4_two_pass needs the TL4_TESTING build of the core"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/model.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

static uint8_t *swap_buffer;
static const uint8_t *swap_source;
static size_t swap_size;

static void swap_in_b(const uint8_t *data, size_t size)
{
    PROMISE(data == swap_buffer && size == swap_size);
    memcpy(swap_buffer, swap_source, swap_size);
}

static int props_equal(const tl4_prop *a, const tl4_prop *b)
{
    return a->area == b->area && a->class_ordinal == b->class_ordinal && a->part == b->part && a->state == b->state &&
           a->name_id == b->name_id && a->type == b->type && a->origin == b->origin && a->ref_where == b->ref_where &&
           a->seq == b->seq && a->reference == b->reference && a->data_off == b->data_off && a->cb == b->cb &&
           a->ref_off == b->ref_off && a->ref_size == b->ref_size;
}

/* The two handles describe the same layout: every field but the base pointer, compared one by one. */
static int handles_equal(const tl4_pe *a, const tl4_pe *b)
{
    return a->size == b->size && a->machine == b->machine && a->nsections == b->nsections && a->is_pe32plus == b->is_pe32plus &&
           a->section_table_off == b->section_table_off && a->rsrc_rva == b->rsrc_rva && a->rsrc_dir_size == b->rsrc_dir_size &&
           a->rsrc_section_va == b->rsrc_section_va && a->rsrc_section_raw == b->rsrc_section_raw &&
           a->rsrc_section_raw_size == b->rsrc_section_raw_size && a->rsrc_off == b->rsrc_off && a->rsrc_limit == b->rsrc_limit &&
           a->ndirectories == b->ndirectories && a->nentries == b->nentries && a->nresources == b->nresources;
}

static void check_within(const tl4_model *m, size_t need)
{
    uint32_t i;
    PROMISE(tl4_model_size(&m->stats) <= need);
    PROMISE(m->nclasses == m->stats.cmap_names);
    PROMISE(m->nprops + m->counts.duplicate_props <= m->stats.variant.records + m->stats.rmap.records);
    PROMISE(m->npartstates <= m->stats.variant.partstates + m->stats.rmap.partstates);
    for (i = 0; i < m->nprops; i++)
    {
        const tl4_prop *p = &m->props[i];
        PROMISE(p->class_ordinal < m->nclasses && p->area <= TL4_AREA_RMAP);
        PROMISE(p->data_off <= m->area_size[p->area] && p->cb <= m->area_size[p->area] - p->data_off);
        PROMISE(m->area_off[p->area] <= m->style.size && m->area_size[p->area] <= m->style.size - m->area_off[p->area]);
        if (p->ref_where == TL4_REF_STYLE) PROMISE(p->ref_off <= m->style.size && p->ref_size <= m->style.size - p->ref_off);
    }
    for (i = 0; i < m->nclasses; i++)
        PROMISE(m->classes[i].base == TL4_NO_BASE || m->classes[i].base < m->nclasses);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    size_t half = size / 2, need, need_b, i;
    const uint8_t *a = data, *b = data + half;
    tl4_pe pe, pe_b;
    tl4_v4_stats s, s_b;
    tl4_model changed, mb;
    uint64_t *buf = 0, *buf_b = 0;
    tl4_status st;

    if (half < 64) return 0;
    swap_buffer = (uint8_t *)malloc(half);
    if (!swap_buffer) return 0;
    memcpy(swap_buffer, a, half);
    swap_source = b;
    swap_size = half;

    if (tl4_pe_open(swap_buffer, half, &pe) != TL4_OK || tl4_v4_validate(&pe, 0, &s) != TL4_OK) { free(swap_buffer); return 0; }
    need = tl4_model_size(&s);
    buf = (uint64_t *)malloc(need);
    if (!buf) { free(swap_buffer); return 0; }

    tl4_model_test_between_passes = swap_in_b;
    st = tl4_model_build(&pe, 0, buf, need, &changed);
    tl4_model_test_between_passes = 0;

    if (st != TL4_OK)
        PROMISE(changed.classes == NULL && changed.nprops == 0 && changed.counts.globals == TL4_NO_BASE);
    else
    {
        check_within(&changed, need);
        /* B on its own, when it validates AND lays its sections and resources out like A: the same model. The swapped
         * build keeps A's offsets, so a B that is valid at other offsets is a different file, not a finding. */
        if (tl4_pe_open(b, half, &pe_b) == TL4_OK && tl4_v4_validate(&pe_b, 0, &s_b) == TL4_OK && handles_equal(&pe, &pe_b))
        {
            need_b = tl4_model_size(&s_b);
            buf_b = (uint64_t *)malloc(need_b);
            if (buf_b && tl4_model_build(&pe_b, 0, buf_b, need_b, &mb) == TL4_OK)
            {
                PROMISE(mb.nprops == changed.nprops && mb.npartstates == changed.npartstates && mb.nclasses == changed.nclasses);
                PROMISE(mb.counts.duplicate_props == changed.counts.duplicate_props && mb.counts.globals == changed.counts.globals);
                for (i = 0; i < mb.nprops; i++) PROMISE(props_equal(&mb.props[i], &changed.props[i]));
                for (i = 0; i < mb.nclasses; i++) PROMISE(mb.classes[i].base == changed.classes[i].base && mb.name_order[i] == changed.name_order[i]);
            }
        }
    }
    free(buf_b); free(buf); free(swap_buffer);
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
    printf("fuzz_v4_two_pass: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
