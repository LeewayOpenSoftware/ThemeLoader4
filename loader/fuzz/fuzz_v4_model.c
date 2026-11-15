/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness: a PE image through open, validate, model build and every query (S2: fuzz_v4_model)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The input is a style image; its last 16 bytes also drive the queries (class, part, state, name id). Promises: the
 * build is deterministic (field compare), its arrays stay within the counts the validator reported, every lookup and
 * getter returns a status and every range it hands out lies inside the mapping; after the input is overwritten
 * (R-GEN-11) the same queries still reject or stay in range. An exact-size buffer makes any over-read fault.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/model.h>
#include <tl4/loader/range.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

static int props_equal(const tl4_prop *a, const tl4_prop *b)
{
    return a->class_ordinal == b->class_ordinal && a->part == b->part && a->state == b->state && a->name_id == b->name_id &&
           a->type == b->type && a->origin == b->origin && a->area == b->area && a->reference == b->reference &&
           a->data_off == b->data_off && a->cb == b->cb && a->seq == b->seq;
}

static int counts_equal(const tl4_model_counts *a, const tl4_model_counts *b)
{
    return a->classes == b->classes && a->descriptors == b->descriptors && a->partstates == b->partstates && a->props == b->props &&
           a->duplicate_props == b->duplicate_props && a->duplicate_names == b->duplicate_names && a->globals == b->globals &&
           a->sysmetrics == b->sysmetrics;
}

static void queries(const tl4_model *m, const uint8_t *q, int changed)
{
    const tl4_pe *pe = &m->style;
    uint32_t i, ord, chars, id, off, size, cnt;
    const uint8_t *s16;
    int32_t v, a[402];
    uint16_t str[128];
    tl4_font f;
    const tl4_prop *p, *p2;
    for (i = 0; i < 4; i++)
    {
        uint32_t cls = m->nclasses ? ((uint32_t)q[4 * i] | ((uint32_t)q[4 * i + 1] << 8)) % (m->nclasses + 1u) : 0;
        uint32_t part = q[4 * i + 2], state = q[4 * i + 3];
        uint32_t name = 1u + (((uint32_t)q[(4 * i + 5) % 16] << 8 | q[(4 * i + 7) % 16]) % 9000u);
        tl4_status st = tl4_model_find(m, cls, part, state, name, &p);
        if (tl4_model_find_rmap(m, cls, part, state, name, &p2) == TL4_OK) PROMISE(p2 >= m->props && p2 < m->props + m->nprops && p2->area == TL4_AREA_RMAP);
        else PROMISE(p2 == NULL);
        if (st != TL4_OK) { PROMISE(p == NULL); continue; }
        PROMISE(p->area == TL4_AREA_VARIANT);
        PROMISE(p >= m->props && p < m->props + m->nprops);
        if (!changed) PROMISE(p->class_ordinal < m->nclasses && p->part <= 255 && p->state <= 255 && p->name_id != 0);
        (void)tl4_get_int(m, p, &v); (void)tl4_get_bool(m, p, &v); (void)tl4_get_enum(m, p, &v); (void)tl4_get_metric(m, p, &v);
        (void)tl4_get_color(m, p, &cnt); (void)tl4_get_position(m, p, a); (void)tl4_get_rect(m, p, a); (void)tl4_get_margins(m, p, a);
        if (tl4_get_intlist(m, p, a, 402, &cnt) == TL4_OK) PROMISE(cnt <= 402);
        if (tl4_get_string(m, p, str, 128, &chars) == TL4_OK) PROMISE(chars < 128 && str[chars] == 0);
        if (tl4_get_font(m, p, &f) == TL4_OK) PROMISE(f.face_chars >= 1 && f.face_chars <= 31 && f.points >= 1 && f.points <= 255 && f.quality <= 6);
        if (tl4_get_filename(m, p, &id, &off, &size) == TL4_OK) PROMISE(id == p->reference && off <= pe->size && size <= pe->size - off);
        if (tl4_get_diskstream(m, p, &id, &off, &size) == TL4_OK) PROMISE(id == p->reference && off <= pe->size && size <= pe->size - off);
        /* R-MOD-7: the range function on every value a getter hands out: one of two answers, the same twice, no crash */
        {
            enum tl4_range_row row = tl4_range_row(p->type, p->name_id);
            tl4_status r1, r2;
            PROMISE(row == tl4_range_row(p->type, p->name_id) && row <= TL4_RANGE_NEVER);
            if (tl4_get_int(m, p, &v) == TL4_OK || tl4_get_bool(m, p, &v) == TL4_OK || tl4_get_enum(m, p, &v) == TL4_OK)
            {
                r1 = tl4_range_check(p->type, p->name_id, &v, 1); r2 = tl4_range_check(p->type, p->name_id, &v, 1);
                PROMISE((r1 == TL4_OK || r1 == TL4_E_RANGE) && r1 == r2);
            }
            if (tl4_get_position(m, p, a) == TL4_OK) { r1 = tl4_range_check(p->type, p->name_id, a, 2); PROMISE(r1 == TL4_OK || r1 == TL4_E_RANGE); }
            if (tl4_get_rect(m, p, a) == TL4_OK || tl4_get_margins(m, p, a) == TL4_OK) { r1 = tl4_range_check(p->type, p->name_id, a, 4); PROMISE(r1 == TL4_OK || r1 == TL4_E_RANGE); }
            if (tl4_get_intlist(m, p, a, 402, &cnt) == TL4_OK) { r1 = tl4_range_check(p->type, p->name_id, a, cnt); PROMISE(r1 == TL4_OK || r1 == TL4_E_RANGE); }
            if (tl4_get_font(m, p, &f) == TL4_OK) PROMISE(tl4_range_check_font(&f) == TL4_OK);                  /* the parser enforces R-RES-2 already */
            if (tl4_get_string(m, p, str, 128, &chars) == TL4_OK) PROMISE(tl4_range_check_string(chars) == TL4_OK);  /* 1..127 here */
            if (row == TL4_RANGE_NEVER) PROMISE(tl4_range_check(p->type, p->name_id, 0, 0) == TL4_E_RANGE);
        }
        (void)tl4_model_part_defined(m, cls, part);
    }
    /* names: every ordinal readable (unchanged input), and the lookup by name finds the lowest ordinal of its name */
    for (ord = 0; ord < m->nclasses && ord < 8; ord++)
    {
        tl4_status st = tl4_model_class_name(m, ord, &s16, &chars);
        if (st == TL4_OK)
        {
            uint32_t found;
            PROMISE(s16 >= pe->base && (size_t)(s16 - pe->base) + 2u * chars <= pe->size);
            if (!changed)
            {
                PROMISE(tl4_model_class(m, s16, chars, &found) == TL4_OK);
                PROMISE(found <= ord);
            }
        }
        else PROMISE(changed || st == TL4_E_NOT_FOUND);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_pe pe;
    tl4_v4_stats s;
    tl4_model m, m2;
    uint8_t *copy, q[16];
    uint64_t *buf, *buf2;
    size_t need, i;
    tl4_status st, st2;

    if (size < 16) return 0;
    memcpy(q, data + size - 16, 16);
    copy = (uint8_t *)malloc(size);
    if (!copy) return 0;
    memcpy(copy, data, size);

    if (tl4_pe_open(copy, size, &pe) != TL4_OK) { PROMISE(pe.base == NULL); free(copy); return 0; }
    if (tl4_v4_validate(&pe, 0, &s) != TL4_OK) { free(copy); return 0; }
    need = tl4_model_size(&s);
    PROMISE(need > 0 && need % 8 == 0);
    buf = (uint64_t *)malloc(need);                                         /* exact size: an overrun faults */
    buf2 = (uint64_t *)malloc(need);
    if (!buf || !buf2) { free(buf); free(buf2); free(copy); return 0; }

    st = tl4_model_build(&pe, 0, buf, need, &m);
    st2 = tl4_model_build(&pe, 0, buf2, need, &m2);
    PROMISE(st == st2);                                                     /* R-GEN-8 */
    if (st != TL4_OK)
    {
        PROMISE(m.classes == NULL && m.nprops == 0 && m.counts.globals == TL4_NO_BASE);   /* R-GEN-5 */
        free(buf); free(buf2); free(copy);
        return 0;
    }
    PROMISE(counts_equal(&m.counts, &m2.counts) && m.nprops == m2.nprops && m.npartstates == m2.npartstates);
    for (i = 0; i < m.nprops; i++) PROMISE(props_equal(&m.props[i], &m2.props[i]));
    for (i = 0; i < m.nclasses; i++) PROMISE(m.name_order[i] == m2.name_order[i] && m.classes[i].base == m2.classes[i].base);
    /* within what the validator reported */
    PROMISE(m.nclasses == s.cmap_names && m.counts.descriptors == s.descriptors);
    PROMISE(m.nprops + m.counts.duplicate_props == s.variant.records + s.rmap.records);
    PROMISE(m.npartstates <= s.variant.partstates + s.rmap.partstates);
    PROMISE(m.counts.duplicate_names < m.nclasses);
    for (i = 0; i < m.nprops; i++)
    {
        const tl4_prop *p = &m.props[i];
        PROMISE(p->class_ordinal < m.nclasses && p->area <= TL4_AREA_RMAP);
        PROMISE(p->reference == 0 || p->cb == 0);
        PROMISE(p->data_off <= m.area_size[p->area] && p->cb <= m.area_size[p->area] - p->data_off);
        PROMISE(p->ref_where <= TL4_REF_STRINGS && (p->reference != 0 || p->ref_where == TL4_REF_UNRESOLVED));
        if (p->ref_where == TL4_REF_STYLE) PROMISE(p->ref_off <= pe.size && p->ref_size <= pe.size - p->ref_off);
        if (i) PROMISE(m.props[i - 1].area <= p->area);
        if (i) PROMISE(!(m.props[i - 1].class_ordinal == p->class_ordinal && m.props[i - 1].part == p->part &&
                         m.props[i - 1].state == p->state && m.props[i - 1].name_id == p->name_id));   /* deduplicated */
    }
    for (i = 0; i < m.npartstates; i++)
        PROMISE(m.partstates[i].first_prop + m.partstates[i].nprops <= m.nprops && m.partstates[i].nprops >= 1);
    for (i = 0; i < m.nclasses; i++)
        PROMISE(m.classes[i].first_ps + m.classes[i].nps <= m.npartstates && (m.classes[i].base == TL4_NO_BASE || m.classes[i].base < m.nclasses));

    queries(&m, q, 0);

    /* the input changes under the model (R-GEN-11): the second half repeated overwrites the buffer */
    for (i = 0; i < size; i++) copy[i] = data[size / 2 + (i % (size - size / 2))];
    queries(&m, q, 1);

    /* the input changes between the sizing validation and the build (MUST 7): the build with the buffer sized for the
     * old bytes fails cleanly or builds a model that is consistent on its own */
    st2 = tl4_model_build(&pe, 0, buf2, need, &m2);
    if (st2 != TL4_OK) PROMISE(m2.classes == NULL && m2.nprops == 0);
    else
    {
        /* the new bytes may describe another style: the model is bounded by its own validation and fits the buffer */
        PROMISE(tl4_model_size(&m2.stats) <= need && m2.nclasses == m2.stats.cmap_names);
        PROMISE(m2.nprops + m2.counts.duplicate_props == m2.stats.variant.records + m2.stats.rmap.records);
        PROMISE(m2.npartstates <= m2.stats.variant.partstates + m2.stats.rmap.partstates);
        queries(&m2, q, 0);
    }

    free(buf); free(buf2); free(copy);
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
    printf("fuzz_v4_model: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
