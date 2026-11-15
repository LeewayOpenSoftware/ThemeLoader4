/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     R-V4-14: a generated style at the limits of REQUIREMENTS section 6 validates inside the fuzz timeout
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The style has 1,024 CMAP names of the maximal length (a 256 KB map), 4,096 IMAGE resources, 32,768 VARIANT
 * records that each carry a FILENAME reference over all the images, 8,192 part-states and BCMAP chains of depth 8.
 * Open and validation together must finish inside the S1 fuzz timeout of 5 s of CPU time in a plain build; under
 * the sanitizers (TL4_SANITIZED) only completion is checked, with a generous cap, so the test is not flaky.
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <tl4/loader/v4.h>
#include "pebuild.h"
#include "v4build.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

#define NCLASSES      (TL4_V4_MAX_CLASSES - 3u)        /* 1,021 classes after the three descriptors */
#define NIMAGES       TL4_V4_MAX_IMAGES                 /* 4,096 */
#define NRECORDS      TL4_V4_MAX_RECORDS                /* 32,768 */
#define NAME_CHARS    (2u * TL4_V4_MAX_NAME_HALF + 2u)  /* "A..59..::B..59.." */

static uint8_t image[2u * 1024u * 1024u];
static char class_names[TL4_V4_MAX_CLASSES][NAME_CHARS + 1];
static const char *name_ptrs[TL4_V4_MAX_CLASSES];
static uint8_t cmap[TL4_V4_MAX_CMAP_BYTES];
static uint8_t vmap[128], bcmap[4u + 4u * NCLASSES], rmap[64];
static uint8_t variant[NRECORDS * 32u];
static uint32_t bases[NCLASSES];
static pb_res res[NIMAGES + 8u];
static const uint8_t png[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
static const uint8_t version4[2] = {4, 0};
static const uint8_t display_name[10] = {'L', 0, 'i', 0, 'm', 0, 'i', 0, 0, 0};

static void make_names(void)
{
    uint32_t i, k;
    const char *desc[3] = {"documentation", "sizevariant.NormalSize", "colorvariant.NormalColor"};
    for (i = 0; i < 3; i++) name_ptrs[i] = desc[i];
    for (i = 3; i < TL4_V4_MAX_CLASSES; i++)
    {
        char *s = class_names[i];
        /* both halves 59 characters: the first encodes the ordinal, so every name is distinct */
        for (k = 0; k < TL4_V4_MAX_NAME_HALF; k++) s[k] = (char)('A' + (k % 26));
        s[0] = (char)('A' + (i / 676) % 26);
        s[1] = (char)('A' + (i / 26) % 26);
        s[2] = (char)('A' + i % 26);
        s[TL4_V4_MAX_NAME_HALF] = ':';
        s[TL4_V4_MAX_NAME_HALF + 1] = ':';
        for (k = 0; k < TL4_V4_MAX_NAME_HALF; k++) s[TL4_V4_MAX_NAME_HALF + 2 + k] = (char)('a' + (k % 26));
        s[NAME_CHARS] = 0;
        name_ptrs[i] = s;
    }
}

/* The records, grouped by class (1,021 class changes), 8 parts per class and 4 records per part-state; the last
 * class takes the remaining 96 records as 24 more part-states: 8,192 part-states in all, every record a FILENAME
 * reference walking over all 4,096 images. */
static uint32_t make_variant(void)
{
    uint32_t off = 0, k = 0, c, part, s;
    for (c = 0; c < NCLASSES; c++)
        for (part = 0; part < 8; part++)
            for (s = 0; s < 4; s++, k++)
                off = v4b_record(variant, sizeof(variant), off, 3001u + s, TL4_V4_FILENAME, 3u + c, part, 0, 1u + (k % NIMAGES), 0, 0);
    for (part = 8; k < NRECORDS; part++)
        for (s = 0; s < 4; s++, k++)
            off = v4b_record(variant, sizeof(variant), off, 3001u + s, TL4_V4_FILENAME, 3u + NCLASSES - 1u, part, 0, 1u + (k % NIMAGES), 0, 0);
    return off;
}

int main(void)
{
    uint32_t n = 0, i, cmap_size, vmap_size, bcmap_size, rmap_size, variant_size;
    size_t bytes;
    pb_options o = {1, 0, 0, 0};
    tl4_pe pe;
    tl4_v4_stats s;
    clock_t t0, t1;
    double seconds;
#ifdef TL4_SANITIZED
    const double cap = 120.0;
#else
    const double cap = 5.0;
#endif

    make_names();
    cmap_size = v4b_cmap(cmap, sizeof(cmap), name_ptrs, TL4_V4_MAX_CLASSES);
    CHECK(cmap_size > 0 && cmap_size <= TL4_V4_MAX_CMAP_BYTES);
    vmap_size = v4b_vmap(vmap, sizeof(vmap), "Normal", "NormalSize", "NormalColor");
    for (i = 0; i < NCLASSES; i++) bases[i] = (i % 9u == 0) ? 0xFFFFFFFFu : i - 1u;   /* chains of depth 8 */
    bcmap_size = v4b_bcmap(bcmap, sizeof(bcmap), bases, NCLASSES);
    rmap_size = v4b_record(rmap, sizeof(rmap), 0, 600, TL4_V4_STRING, 0, 0, 0, 0, display_name, sizeof(display_name));
    variant_size = make_variant();
    CHECK(variant_size == NRECORDS * 32u);

    res[n].type_name = "PACKTHEM_VERSION"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 1; res[n].language = 0; res[n].data = version4; res[n].size = 2; n++;
    res[n].type_name = "CMAP"; res[n].type_id = 0; res[n].name_name = "CMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = cmap; res[n].size = cmap_size; n++;
    res[n].type_name = "VMAP"; res[n].type_id = 0; res[n].name_name = "VMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = vmap; res[n].size = vmap_size; n++;
    res[n].type_name = "BCMAP"; res[n].type_id = 0; res[n].name_name = "BCMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = bcmap; res[n].size = bcmap_size; n++;
    res[n].type_name = "RMAP"; res[n].type_id = 0; res[n].name_name = "RMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = rmap; res[n].size = rmap_size; n++;
    res[n].type_name = "VARIANT"; res[n].type_id = 0; res[n].name_name = "NORMAL"; res[n].name_id = 0; res[n].language = 0; res[n].data = variant; res[n].size = variant_size; n++;
    for (i = 0; i < NIMAGES; i++)
    {
        res[n].type_name = "IMAGE"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 1u + i; res[n].language = 0; res[n].data = png; res[n].size = 8; n++;
    }
    bytes = pb_build(res, n, &o, image, sizeof(image), 0);
    CHECK(bytes > 0);
    printf("limits style: %u bytes, CMAP %u bytes, %u records, %u images\n", (unsigned)bytes, cmap_size, NRECORDS, NIMAGES);

    t0 = clock();
    CHECK_STATUS(tl4_pe_open(image, bytes, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &s), TL4_OK);
    t1 = clock();
    seconds = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;
    printf("open + validate: %.3f s CPU (cap %.0f s)\n", seconds, cap);
    CHECK(seconds < cap);

    CHECK(s.cmap_names == TL4_V4_MAX_CLASSES && s.descriptors == 3 && s.bcmap_entries == NCLASSES && s.bcmap_max_depth == TL4_V4_MAX_CHAIN);
    CHECK(s.variant.records == NRECORDS && s.variant.classes == NCLASSES && s.variant.partstates == TL4_V4_MAX_PARTSTATES);
    CHECK(s.rmap.records == 1 && s.images == NIMAGES && s.streams == 0);
    CHECK(s.filenames == NRECORDS && s.unresolved_filenames == 0 && s.unresolved_fonts == 0);
    CHECK(pe.nresources == NIMAGES + 6u);

    /* the lookups behind the resolution: one image by id, the extremes and a miss */
    {
        tl4_resource r;
        tl4_res_name t = tl4_res_ascii("image"), nm;
        uint32_t cnt = 0;
        nm = tl4_res_id(1);       CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_OK); CHECK(r.size == 8 && memcmp(r.data, png, 8) == 0);
        nm = tl4_res_id(NIMAGES); CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_OK);
        nm = tl4_res_id(2048);    CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_OK);
        nm = tl4_res_id(0);       CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_E_NOT_FOUND);
        nm = tl4_res_id(NIMAGES + 1u); CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_E_NOT_FOUND);
        CHECK_STATUS(tl4_pe_count_type(&pe, &t, &cnt), TL4_OK); CHECK(cnt == NIMAGES);
    }

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
