/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Unit tests of the property model and the query subset (milestone L2, REQUIREMENTS section 5)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <tl4/loader/model.h>
#include "v4fixture.h"
#include "v4build.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

static uint8_t image[64 * 1024];
static v4f_parts parts;
static uint64_t buffer[8192];        /* 64 KB, 8-byte aligned */
static tl4_pe pe;
static tl4_model model;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Build the image from the parts, open it and build the model. */
static tl4_status build(void)
{
    size_t n = v4f_build(&parts, image, sizeof(image));
    tl4_status st;
    if (n == 0) return TL4_E_ARG;
    if ((st = tl4_pe_open(image, n, &pe)) != TL4_OK) return st;
    return tl4_model_build(&pe, 0, buffer, sizeof(buffer), &model);
}

static int name_is(const uint8_t *utf16, uint32_t chars, const char *ascii)
{
    uint32_t i;
    if (chars != strlen(ascii)) return 0;
    for (i = 0; i < chars; i++) if (utf16[2 * i] != (uint8_t)ascii[i] || utf16[2 * i + 1] != 0) return 0;
    return 1;
}

static void test_shape(void)
{
    const uint8_t *s16; uint32_t chars, ord;
    v4f_default(&parts);
    CHECK_STATUS(build(), TL4_OK);
    CHECK(model.counts.classes == 6 && model.counts.descriptors == 3);
    CHECK(model.counts.props == 15 && model.counts.duplicate_props == 0 && model.counts.duplicate_names == 0);
    CHECK(model.counts.partstates == 7);   /* RMAP (0,0,0); globals (3,0,0); Button (4,1,0) (4,1,1) (4,1,2) (4,2,0); Explorer::Button (5,0,0) */
    CHECK(model.counts.globals == 3 && model.counts.sysmetrics == TL4_NO_BASE);
    CHECK(model.classes[3].base == TL4_NO_BASE && model.classes[4].base == 3 && model.classes[5].base == 4);
    CHECK(model.classes[4].nps == 4 && model.classes[5].nps == 1 && model.classes[1].nps == 0);
    CHECK(model.rmap_nps == 1 && model.partstates[model.rmap_first_ps].area == TL4_AREA_RMAP && model.classes[0].nps == 0);
    CHECK(model.counts.unresolved == 0 && model.has_strings == 0 && model.style.base == image);
    CHECK(model.partstates[model.classes[4].first_ps].part == 1 && model.partstates[model.classes[4].first_ps].state == 0);
    CHECK_STATUS(tl4_model_class_name(&model, 5, &s16, &chars), TL4_OK); CHECK(name_is(s16, chars, "Explorer::Button"));
    CHECK_STATUS(tl4_model_class_name(&model, 6, &s16, &chars), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_class_ascii(&model, "Button", &ord), TL4_OK); CHECK(ord == 4);
    CHECK_STATUS(tl4_model_class_ascii(&model, "Explorer::Button", &ord), TL4_OK); CHECK(ord == 5);
    CHECK_STATUS(tl4_model_class_ascii(&model, "globals", &ord), TL4_OK); CHECK(ord == 3);
    CHECK_STATUS(tl4_model_class_ascii(&model, "button", &ord), TL4_E_NOT_FOUND);        /* exact, no folding */
    CHECK_STATUS(tl4_model_class_ascii(&model, "Butto", &ord), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_class_ascii(&model, "Buttons", &ord), TL4_E_NOT_FOUND);
    CHECK(tl4_model_size(&model.stats) <= sizeof(buffer));
    /* props sorted by (class, part, state, name) */
    {
        uint32_t i, ok = 1;
        for (i = 1; i < model.nprops; i++)
        {
            const tl4_prop *a = &model.props[i - 1], *b = &model.props[i];
            uint64_t ka = ((uint64_t)a->area << 56) | ((uint64_t)a->class_ordinal << 40) | ((uint64_t)a->part << 32) | ((uint64_t)a->state << 16) | a->name_id;
            uint64_t kb = ((uint64_t)b->area << 56) | ((uint64_t)b->class_ordinal << 40) | ((uint64_t)b->part << 32) | ((uint64_t)b->state << 16) | b->name_id;
            if (ka >= kb) ok = 0;
        }
        CHECK(ok);
    }
}

static void test_search_order(void)
{
    const tl4_prop *p;
    v4f_default(&parts);
    CHECK_STATUS(build(), TL4_OK);
    /* exact (part, state) in the class itself */
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 2, 3402, &p), TL4_OK); CHECK(p->origin == TL4_PO_STATE && p->type == TL4_V4_RECT);
    /* Explorer::Button (5) has no (1,2): the chain reaches Button (4), which has 3402 at (1,2) */
    CHECK_STATUS(tl4_model_find(&model, 5, 1, 2, 3402, &p), TL4_OK); CHECK(p->class_ordinal == 4 && p->origin == TL4_PO_STATE);
    /* (1,2) nowhere has 4001; (1,0) in Button does: origin PART */
    CHECK_STATUS(tl4_model_find(&model, 5, 1, 2, 4001, &p), TL4_OK); CHECK(p->class_ordinal == 4 && p->origin == TL4_PO_PART);
    /* a class-level property of Explorer::Button itself, asked with part and state: third step, origin CLASS */
    CHECK_STATUS(tl4_model_find(&model, 5, 1, 2, 3805, &p), TL4_OK); CHECK(p->class_ordinal == 5 && p->origin == TL4_PO_CLASS);
    /* globals at the end of the chain: origin GLOBAL */
    CHECK_STATUS(tl4_model_find(&model, 5, 1, 2, 3804, &p), TL4_OK); CHECK(p->class_ordinal == 3 && p->origin == TL4_PO_GLOBAL);
    CHECK_STATUS(tl4_model_find(&model, 5, 0, 0, 3804, &p), TL4_OK); CHECK(p->origin == TL4_PO_GLOBAL);
    /* a descriptor name (no base, no part-states) still reaches globals */
    CHECK_STATUS(tl4_model_find(&model, 1, 0, 0, 3804, &p), TL4_OK); CHECK(p->class_ordinal == 3);
    /* the RMAP display name answers documentation queries only; a VARIANT record never answers an RMAP query (R-MOD-6) */
    CHECK_STATUS(tl4_model_find(&model, 0, 0, 0, 600, &p), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_find_rmap(&model, 0, 0, 0, 600, &p), TL4_OK); CHECK(p->area == TL4_AREA_RMAP && p->type == TL4_V4_STRING);
    CHECK_STATUS(tl4_model_find_rmap(&model, 3, 0, 0, 3804, &p), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_find_rmap(&model, 0, 0, 0, 601, &p), TL4_E_NOT_FOUND);
    /* misses and arguments */
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 2, 9999, &p), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_find(&model, 6, 0, 0, 3804, &p), TL4_E_NOT_FOUND);
    CHECK_STATUS(tl4_model_find(&model, 4, 256, 0, 3804, &p), TL4_E_ARG);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 0, 0, &p), TL4_E_ARG);
    CHECK(tl4_model_part_defined(&model, 4, 1) && tl4_model_part_defined(&model, 4, 2) && !tl4_model_part_defined(&model, 4, 3));
    CHECK(tl4_model_part_defined(&model, 5, 1));            /* through the chain, as MSSTYLES_FindPart walks overrides */
    CHECK(tl4_model_part_defined(&model, 5, 0) && !tl4_model_part_defined(&model, 1, 1));
}

static void test_getters(void)
{
    const tl4_prop *p;
    int32_t v, a[4];
    uint32_t c, n, id, off, size;
    uint16_t str[64];
    tl4_font f;
    v4f_default(&parts);
    CHECK_STATUS(build(), TL4_OK);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 3804, &p), TL4_OK);
    CHECK_STATUS(tl4_get_color(&model, p, &c), TL4_OK); CHECK(c == 0x1234);
    CHECK_STATUS(tl4_get_int(&model, p, &v), TL4_E_TYPE);
    CHECK_STATUS(tl4_get_metric(&model, p, &v), TL4_OK); CHECK(v == 0x1234);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 1001, &p), TL4_OK);
    CHECK_STATUS(tl4_get_bool(&model, p, &v), TL4_OK); CHECK(v == 1);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 2401, &p), TL4_OK);
    CHECK_STATUS(tl4_get_int(&model, p, &v), TL4_OK); CHECK(v == 1);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 0, 4001, &p), TL4_OK);
    CHECK_STATUS(tl4_get_enum(&model, p, &v), TL4_OK); CHECK(v == 1);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 0, 3601, &p), TL4_OK);
    CHECK_STATUS(tl4_get_margins(&model, p, a), TL4_OK); CHECK(a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 4);
    CHECK_STATUS(tl4_get_metric(&model, p, &v), TL4_OK); CHECK(v == 1);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 1, 3401, &p), TL4_OK);
    CHECK_STATUS(tl4_get_position(&model, p, a), TL4_OK); CHECK(a[0] == 5 && a[1] == 6);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 2, 3402, &p), TL4_OK);
    CHECK_STATUS(tl4_get_rect(&model, p, a), TL4_OK); CHECK(a[0] == 1 && a[1] == 2 && a[2] == 3 && a[3] == 4);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 2, 2201, &p), TL4_OK);
    CHECK_STATUS(tl4_get_int(&model, p, &v), TL4_OK); CHECK(v == 1);     /* SIZE through the INT getter */
    CHECK_STATUS(tl4_model_find(&model, 4, 2, 0, 6000, &p), TL4_OK);
    CHECK_STATUS(tl4_get_intlist(&model, p, a, 4, &n), TL4_OK); CHECK(n == 2 && a[0] == 7 && a[1] == 8);
    CHECK_STATUS(tl4_get_intlist(&model, p, a, 1, &n), TL4_E_BUFFER);
    CHECK_STATUS(tl4_get_metric(&model, p, &v), TL4_OK); CHECK(v == 7);
    CHECK_STATUS(tl4_model_find_rmap(&model, 0, 0, 0, 600, &p), TL4_OK);
    CHECK_STATUS(tl4_get_string(&model, p, str, 64, &n), TL4_OK); CHECK(n == 4 && str[0] == 'A' && str[3] == 'o' && str[4] == 0);
    CHECK_STATUS(tl4_get_string(&model, p, str, 5, &n), TL4_OK); CHECK(n == 4);
    CHECK_STATUS(tl4_get_string(&model, p, str, 4, &n), TL4_E_BUFFER);
    CHECK_STATUS(tl4_get_metric(&model, p, &v), TL4_E_TYPE);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 803, &p), TL4_OK);
    CHECK(p->ref_where == TL4_REF_STYLE && p->ref_size == 60 && p->cb == 0);        /* resolved at build (R-MOD-4) */
    CHECK_STATUS(tl4_get_font(&model, p, &f), TL4_OK);
    CHECK(f.face_chars == 8 && f.face[0] == 'S' && f.face[7] == 'I' && f.face[8] == 0 && f.points == 9 && f.quality == 5 && !f.bold);
    CHECK_STATUS(tl4_get_string(&model, p, str, 64, &n), TL4_E_TYPE);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 0, 3001, &p), TL4_OK);
    CHECK_STATUS(tl4_get_filename(&model, p, &id, &off, &size), TL4_OK); CHECK(id == 508 && size == 32 && off + size <= pe.size);
    CHECK(memcmp(image + off, parts.image, 32) == 0);
    CHECK_STATUS(tl4_get_diskstream(&model, p, &id, &off, &size), TL4_E_TYPE);
    CHECK_STATUS(tl4_model_find(&model, 5, 0, 0, 8000, &p), TL4_OK);
    CHECK_STATUS(tl4_get_diskstream(&model, p, &id, &off, &size), TL4_OK); CHECK(id == 1718 && size == 32);
    CHECK_STATUS(tl4_get_color(&model, p, &c), TL4_E_TYPE);                /* a reference record has no inline value */
    CHECK_STATUS(tl4_get_int(&model, 0, &v), TL4_E_ARG);
    CHECK_STATUS(tl4_get_intlist(&model, p, 0, 0, &n), TL4_E_TYPE);
}

static void test_duplicates(void)
{
    const tl4_prop *p;
    uint32_t c, off, ord;
    static const uint8_t other[4] = {0x78, 0x56, 0, 0};
    static const char *const dup[] = {"documentation", "sizevariant.NormalSize", "colorvariant.NormalColor",
                                      "globals", "Button", "Button"};
    /* a second 3804 at globals: the first in file order wins and the second is counted (R-V4-9) */
    v4f_default(&parts);
    off = v4b_record(parts.variant, sizeof(parts.variant), parts.variant_size, 3804, 204, 3, 0, 0, 0, other, 4);
    parts.variant_size = off;
    CHECK_STATUS(build(), TL4_OK);
    CHECK(model.counts.duplicate_props == 1 && model.counts.props == 15);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 3804, &p), TL4_OK);
    CHECK_STATUS(tl4_get_color(&model, p, &c), TL4_OK); CHECK(c == 0x1234);
    /* a repeated class name: both ordinals valid, the lookup by name takes the lowest (R-V4-2), counted once */
    v4f_default(&parts);
    parts.cmap_size = v4b_cmap(parts.cmap, sizeof(parts.cmap), dup, 6);
    CHECK_STATUS(build(), TL4_OK);
    CHECK(model.counts.duplicate_names == 1 && model.counts.classes == 6);
    CHECK_STATUS(tl4_model_class_ascii(&model, "Button", &ord), TL4_OK); CHECK(ord == 4);
    CHECK_STATUS(tl4_model_find(&model, 5, 0, 0, 8000, &p), TL4_OK);     /* ordinal 5 keeps its records */
}

static void test_sizes_and_failures(void)
{
    tl4_v4_stats s;
    size_t need;
    v4f_default(&parts);
    CHECK_STATUS(build(), TL4_OK);
    need = tl4_model_size(&model.stats);
    CHECK(need > 0 && need % 8 == 0);
    CHECK_STATUS(tl4_model_build(&pe, 0, buffer, need - 8, &model), TL4_E_MODEL_SIZE);
    CHECK(model.classes == 0 && model.nprops == 0 && model.counts.globals == TL4_NO_BASE);   /* cleared (R-GEN-5) */
    CHECK_STATUS(tl4_model_build(&pe, 0, buffer, need, &model), TL4_OK);
    CHECK_STATUS(tl4_model_build(&pe, 0, (uint8_t *)buffer + 4, sizeof(buffer) - 4, &model), TL4_E_ARG);  /* alignment */
    CHECK_STATUS(tl4_model_build(0, 0, buffer, sizeof(buffer), &model), TL4_E_ARG);
    CHECK_STATUS(tl4_model_build(&pe, 0, buffer, sizeof(buffer), 0), TL4_E_ARG);
    memset(&s, 0, sizeof(s));
    CHECK(tl4_model_size(&s) == 0 && tl4_model_size(0) == 0);
    /* an invalid style never builds */
    v4f_default(&parts); parts.version[0] = 3;
    CHECK_STATUS(build(), TL4_E_V4_VERSION);
    CHECK(model.classes == 0);
    /* an empty VARIANT builds an empty model with the RMAP record only */
    v4f_default(&parts); parts.variant_size = 0;
    CHECK_STATUS(build(), TL4_OK);
    CHECK(model.counts.props == 1 && model.counts.partstates == 1);
    /* the bytes change between the sizing validation and the build (R-MOD-5): a smaller style builds into the buffer
     * sized for the larger one and is bounded by its own statistics; a larger style into the smaller buffer is refused */
    {
        size_t small = tl4_model_size(&model.stats);
        v4f_default(&parts);
        CHECK_STATUS(build(), TL4_OK);
        need = tl4_model_size(&model.stats);
        CHECK(small < need);
        v4f_default(&parts); parts.variant_size = 0;
        CHECK(v4f_build(&parts, image, sizeof(image)) > 0);               /* the bytes under `pe` are now the small style */
        CHECK_STATUS(tl4_model_build(&pe, 0, buffer, need, &model), TL4_OK);
        CHECK(model.counts.props == 1 && tl4_model_size(&model.stats) == small);
        v4f_default(&parts);
        CHECK(v4f_build(&parts, image, sizeof(image)) > 0);               /* and now the large one again */
        CHECK_STATUS(tl4_model_build(&pe, 0, buffer, small, &model), TL4_E_MODEL_SIZE);
        CHECK(model.classes == 0);
    }
}

/* R-GEN-11: the bytes change after the build; every getter rejects or stays inside the mapping, and every check
 * runs on the copy (R-MOD-4) */
static void test_changed_input(void)
{
    const tl4_prop *p;
    int32_t a[402], v;
    uint32_t n, id, off, size;
    uint16_t str[64];
    tl4_font f;
    size_t bytes;
    v4f_default(&parts);
    CHECK_STATUS(build(), TL4_OK);
    bytes = pe.size;
    /* the INTLIST count word is not read: the model's size rules (R-MOD-4) */
    CHECK_STATUS(tl4_model_find(&model, 4, 2, 0, 6000, &p), TL4_OK);
    put32(image + model.area_off[TL4_AREA_VARIANT] + p->data_off, 500);
    CHECK_STATUS(tl4_get_intlist(&model, p, a, 402, &n), TL4_OK); CHECK(n == 2 && a[0] == 7 && a[1] == 8);
    CHECK_STATUS(tl4_get_metric(&model, p, &v), TL4_OK); CHECK(v == 7);
    put32(image + model.area_off[TL4_AREA_VARIANT] + p->data_off, 2);
    /* the STRING terminator is checked on the copy */
    CHECK_STATUS(tl4_model_find_rmap(&model, 0, 0, 0, 600, &p), TL4_OK);
    image[model.area_off[TL4_AREA_RMAP] + p->data_off + p->cb - 2] = 'x';
    CHECK_STATUS(tl4_get_string(&model, p, str, 64, &n), TL4_E_TRUNCATED);
    image[model.area_off[TL4_AREA_RMAP] + p->data_off + p->cb - 2] = 0;
    CHECK_STATUS(tl4_get_string(&model, p, str, 64, &n), TL4_OK);
    /* the font string is copied, then parsed: a changed string is a syntax error, never a read past the copy */
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 803, &p), TL4_OK);
    CHECK(p->ref_where == TL4_REF_STYLE);
    image[p->ref_off + 2 * 10] = '#';                                     /* "Segoe UI, #, ..." */
    CHECK_STATUS(tl4_get_font(&model, p, &f), TL4_E_FONT_SYNTAX);
    image[p->ref_off + 2 * 10] = '9';
    CHECK_STATUS(tl4_get_font(&model, p, &f), TL4_OK);
    /* overwrite the whole image with noise: no getter may read outside (ASan), every call returns a status */
    memset(image, 0xA5, bytes);
    CHECK_STATUS(tl4_model_find(&model, 3, 0, 0, 803, &p), TL4_OK);
    CHECK_STATUS(tl4_get_font(&model, p, &f), TL4_E_FONT_SYNTAX);
    CHECK_STATUS(tl4_model_find(&model, 4, 1, 0, 3001, &p), TL4_OK);
    CHECK_STATUS(tl4_get_filename(&model, p, &id, &off, &size), TL4_OK);  /* a range resolved at build, still inside */
    CHECK(off + size <= bytes);
    CHECK_STATUS(tl4_model_find_rmap(&model, 0, 0, 0, 600, &p), TL4_OK);
    CHECK_STATUS(tl4_get_string(&model, p, str, 64, &n), TL4_E_TRUNCATED); /* no NUL in the noise */
    (void)tl4_model_class_ascii(&model, "Button", &n);
    CHECK(1);
}

static void test_font_syntax(void)
{
    tl4_font f;
    static const char *const good[] = {"Segoe UI, 9, Quality:ClearType", "Segoe UI, 9, Bold, Quality:ClearType",
                                       "Segoe UI, 12, Underline, Quality:ClearType", "Tahoma,8", " Tahoma , 8 , Italic , Strikeout , Quality: Antialiased "};
    static const char *const bad[] = {"", "Tahoma", "Tahoma, 0", "Tahoma, 256", "Tahoma, 9x", ", 9", "Tahoma, 9, Wide",
                                      "Tahoma, 9, Quality:Fuzzy", "Tahoma, 9, , Bold", "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdef, 9"};
    uint8_t s[2 * 256];
    uint32_t i, k, n;
    for (i = 0; i < sizeof(good) / sizeof(good[0]); i++)
    {
        for (n = 0; good[i][n]; n++) { s[2 * n] = (uint8_t)good[i][n]; s[2 * n + 1] = 0; }
        CHECK_STATUS(tl4_parse_font(s, n, &f), TL4_OK);
    }
    CHECK(f.face_chars == 6 && f.points == 8 && f.italic && f.strikeout && !f.bold && f.quality == 4);
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    {
        for (n = 0; bad[i][n]; n++) { s[2 * n] = (uint8_t)bad[i][n]; s[2 * n + 1] = 0; }
        k = n;
        CHECK_STATUS(tl4_parse_font(s, k, &f), k == 0 ? TL4_E_ARG : TL4_E_FONT_SYNTAX);
    }
    CHECK_STATUS(tl4_parse_font(s, 256, &f), TL4_E_ARG);
    CHECK_STATUS(tl4_parse_font(0, 5, &f), TL4_E_ARG);
    CHECK_STATUS(tl4_parse_font(s, 5, 0), TL4_E_ARG);
}

int main(void)
{
    test_shape();
    test_search_order();
    test_getters();
    test_duplicates();
    test_sizes_and_failures();
    test_changed_input();
    test_font_syntax();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
