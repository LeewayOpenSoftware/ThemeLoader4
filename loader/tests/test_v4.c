/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Unit tests of the version-4 container validator (REQUIREMENTS section 4), one negative per rule
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <tl4/loader/v4.h>
#include "v4fixture.h"
#include "v4build.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

static uint8_t image[64 * 1024];
static v4f_parts parts;

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* Build the image from the current parts and validate it. */
static tl4_status run(tl4_v4_stats *stats)
{
    tl4_pe pe;
    size_t n = v4f_build(&parts, image, sizeof(image));
    tl4_status st;
    if (n == 0) return TL4_E_ARG;
    st = tl4_pe_open(image, n, &pe);
    if (st != TL4_OK) return st;
    return tl4_v4_validate(&pe, 0, stats);
}

static void test_good(void)
{
    tl4_v4_stats s;
    v4f_default(&parts);
    CHECK_STATUS(run(&s), TL4_OK);
    CHECK(s.cmap_names == 6 && s.descriptors == 3 && s.bcmap_entries == 3 && s.bcmap_max_depth == 2);
    CHECK(s.variant.records == 14 && s.rmap.records == 1);
    CHECK(s.variant.dropped_unknown_type == 0 && s.variant.dropped_ref_on_value == 0);
    CHECK(s.variant.classes == 3 && s.variant.partstates == 6);   /* (3,0,0) (4,1,0) (4,1,1) (4,1,2) (4,2,0) (5,0,0) */
    CHECK(s.images == 2 && s.streams == 1 && s.string_blocks == 1);
    CHECK(s.fonts == 1 && s.filenames == 2 && s.diskstreams == 1 && s.strings == 0);
    CHECK(s.unresolved_fonts == 0 && s.unresolved_filenames == 0 && s.unresolved_diskstreams == 0);
    CHECK(s.variant.max_part == 2 && s.variant.max_state == 2 && s.variant.max_cb == 92);
}

/* R-GEN-5: a rejected style leaves the statistics cleared */
static void expect_reject(tl4_status want, const char *what)
{
    tl4_v4_stats s;
    tl4_status st = run(&s);
    checks++;
    if (st != want) { failures++; printf("FAIL %s: %s -> %s, wanted %s\n", __FILE__, what, tl4_status_name(st), tl4_status_name(want)); }
    checks++;
    if (s.cmap_names != 0 || s.variant.records != 0 || s.images != 0) { failures++; printf("FAIL %s: %s left statistics behind\n", __FILE__, what); }
}

static void test_version(void)
{
    v4f_default(&parts); parts.version[0] = 3; expect_reject(TL4_E_V4_VERSION, "R-V4-1 version 3");
    v4f_default(&parts); parts.version[0] = 5; expect_reject(TL4_E_V4_VERSION, "R-V4-1 version 5");
}

static void test_cmap(void)
{
    static const char *const dup[] = {"documentation", "globals", "Button", "Button"};
    static const char *const two[] = {"documentation", "globals", "A::B::C"};
    static const char *const empty_half[] = {"documentation", "globals", "::Button"};
    static const char *const long_half[] = {"documentation", "globals",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA::Button"}; /* 60 chars */
    tl4_v4_cmap cm;
    tl4_v4_stats s;
    uint8_t buf[512];
    uint32_t n;

    v4f_default(&parts); parts.cmap_size -= 1; expect_reject(TL4_E_V4_CMAP, "R-V4-2 odd size");
    v4f_default(&parts); parts.cmap[parts.cmap_size - 2] = 'x'; parts.cmap[parts.cmap_size - 1] = 0; expect_reject(TL4_E_V4_CMAP, "R-V4-2 unterminated last name");
    n = v4b_cmap(buf, sizeof(buf), dup, 4); CHECK(n > 0);
    CHECK_STATUS(tl4_v4_check_cmap(buf, n, &cm), TL4_E_V4_CMAP);              /* duplicate */
    n = v4b_cmap(buf, sizeof(buf), two, 3); CHECK_STATUS(tl4_v4_check_cmap(buf, n, &cm), TL4_E_V4_CMAP);        /* two :: */
    n = v4b_cmap(buf, sizeof(buf), empty_half, 3); CHECK_STATUS(tl4_v4_check_cmap(buf, n, &cm), TL4_E_V4_CMAP); /* empty half */
    n = v4b_cmap(buf, sizeof(buf), long_half, 3); CHECK_STATUS(tl4_v4_check_cmap(buf, n, &cm), TL4_E_V4_CMAP);  /* 60 > 59 */
    CHECK_STATUS(tl4_v4_check_cmap(buf, 0, &cm), TL4_E_V4_CMAP);
    CHECK_STATUS(tl4_v4_check_cmap(NULL, 8, &cm), TL4_E_ARG);
    /* a good map: ordinals resolve to the names, the ordinal past the end does not */
    v4f_default(&parts);
    CHECK_STATUS(tl4_v4_check_cmap(parts.cmap, parts.cmap_size, &cm), TL4_OK);
    CHECK(cm.count == 6);
    {
        const uint8_t *s16; uint32_t chars;
        CHECK_STATUS(tl4_v4_cmap_name(&cm, 4, &s16, &chars), TL4_OK);
        CHECK(chars == 6 && s16[0] == 'B' && s16[10] == 'n');
        CHECK_STATUS(tl4_v4_cmap_name(&cm, 6, &s16, &chars), TL4_E_NOT_FOUND);
    }
    /* a record naming a class beyond the map */
    v4f_default(&parts); put32(parts.variant + 8, 6); expect_reject(TL4_E_V4_RECORD, "R-V4-5 classId == count");
    (void)s;
}

static void test_vmap(void)
{
    tl4_v4_vmap vm;
    v4f_default(&parts);
    CHECK_STATUS(tl4_v4_check_vmap(parts.vmap, parts.vmap_size, &vm), TL4_OK);
    CHECK(vm.variant_chars == 6 && vm.size_chars == 10 && vm.color_chars == 11 && parts.vmap_size == 76);
    v4f_default(&parts); put32(parts.vmap, 200); expect_reject(TL4_E_V4_VMAP, "R-V4-3 prefix over 64");
    v4f_default(&parts); put32(parts.vmap, 1); expect_reject(TL4_E_V4_VMAP, "R-V4-3 prefix 1 (no text)");
    v4f_default(&parts); parts.vmap[4 + 2 * 6] = 'X'; expect_reject(TL4_E_V4_VMAP, "R-V4-3 missing NUL");
    v4f_default(&parts); parts.vmap_size -= 4; expect_reject(TL4_E_V4_VMAP, "R-V4-3 third string cut");
    v4f_default(&parts); put32(parts.vmap, 7); put32(parts.vmap + 20, 0x7FFFFFFFu); expect_reject(TL4_E_V4_VMAP, "R-V4-3 huge prefix");
}

static void test_bcmap(void)
{
    uint32_t cyc[3] = {1, 2, 0}, self[1] = {0}, deep[10] = {0xFFFFFFFFu, 0, 1, 2, 3, 4, 5, 6, 7, 8}, bad[3] = {0xFFFFFFFFu, 7, 0};
    tl4_v4_bcmap bm;
    uint8_t buf[128];
    uint32_t n, base;

    v4f_default(&parts);
    CHECK_STATUS(tl4_v4_check_bcmap(parts.bcmap, parts.bcmap_size, 6, &bm), TL4_OK);
    CHECK(bm.count == 3 && bm.shift == 3 && bm.max_depth == 2);
    CHECK_STATUS(tl4_v4_bcmap_base(&bm, 5, &base), TL4_OK); CHECK(base == 4);     /* Explorer::Button -> Button */
    CHECK_STATUS(tl4_v4_bcmap_base(&bm, 4, &base), TL4_OK); CHECK(base == 3);     /* Button -> globals */
    CHECK_STATUS(tl4_v4_bcmap_base(&bm, 3, &base), TL4_OK); CHECK(base == 0xFFFFFFFFu);
    CHECK_STATUS(tl4_v4_bcmap_base(&bm, 1, &base), TL4_OK); CHECK(base == 0xFFFFFFFFu); /* a descriptor */
    CHECK_STATUS(tl4_v4_bcmap_base(&bm, 6, &base), TL4_E_NOT_FOUND);
    n = v4b_bcmap(buf, sizeof(buf), cyc, 3);  CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 6, &bm), TL4_E_V4_BCMAP);  /* cycle */
    n = v4b_bcmap(buf, sizeof(buf), self, 1); CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 4, &bm), TL4_E_V4_BCMAP);  /* self */
    n = v4b_bcmap(buf, sizeof(buf), deep, 10); CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 13, &bm), TL4_E_V4_BCMAP); /* depth 9 */
    deep[9] = 0xFFFFFFFFu; deep[8] = 0xFFFFFFFFu;                                                                 /* depth 8 passes */
    n = v4b_bcmap(buf, sizeof(buf), deep, 10); CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 13, &bm), TL4_OK); CHECK(bm.max_depth == 7);
    n = v4b_bcmap(buf, sizeof(buf), bad, 3);  CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 6, &bm), TL4_E_V4_BCMAP);  /* entry >= N */
    n = v4b_bcmap(buf, sizeof(buf), cyc, 3);  CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 2, &bm), TL4_E_V4_BCMAP);  /* N > CMAP */
    CHECK_STATUS(tl4_v4_check_bcmap(buf, n, 20, &bm), TL4_E_V4_BCMAP);                                           /* shift 17 > 8 */
    CHECK_STATUS(tl4_v4_check_bcmap(buf, n - 4, 6, &bm), TL4_E_V4_BCMAP);                                        /* count != size */
    CHECK_STATUS(tl4_v4_check_bcmap(buf, 6, 6, &bm), TL4_E_V4_BCMAP);                                            /* size % 4 */
    v4f_default(&parts); parts.omit_bcmap = 1; expect_reject(TL4_E_V4_MISSING, "BCMAP absent");
}

static void test_records(void)
{
    uint8_t *rec;
    /* the first VARIANT record is COLOR at class 3 with 4 inline bytes */
    v4f_default(&parts); rec = parts.variant;
    put32(rec + 0, 0);           expect_reject(TL4_E_V4_RECORD, "R-V4-5 nameId 0");
    v4f_default(&parts); put32(parts.variant + 0, 0x10000);  expect_reject(TL4_E_V4_RECORD, "R-V4-5 nameId 65536");
    v4f_default(&parts); put32(parts.variant + 4, 199);      expect_reject(TL4_E_V4_RECORD, "R-V4-5 type 199");
    v4f_default(&parts); put32(parts.variant + 12, 256);     expect_reject(TL4_E_V4_RECORD, "R-V4-5 part 256");
    v4f_default(&parts); put32(parts.variant + 16, 0x80000000u); expect_reject(TL4_E_V4_RECORD, "R-V4-5/R-GEN-4 negative state");
    {
        tl4_v4_stats s;   /* the seventh field is not interpreted, in inline and in reference records (R-V4-5) */
        v4f_default(&parts); put32(parts.variant + 24, 1); put32(parts.variant + 3 * 40 + 24, 5505138u);
        CHECK_STATUS(run(&s), TL4_OK);
        CHECK(s.variant.records == 14);
    }
    v4f_default(&parts); put32(parts.variant + 28, 0x10001); expect_reject(TL4_E_V4_RECORD, "R-V4-5 cbData over 64 KB");
    v4f_default(&parts); put32(parts.variant + 28, 8);       expect_reject(TL4_E_V4_SIZE, "R-V4-6 COLOR with 8 bytes");
    v4f_default(&parts); put32(parts.variant + 28, 2000);    expect_reject(TL4_E_V4_RECORD, "R-V4-5 inline data past the area");
    /* walk end: a trailing partial header, and an area that is not a multiple of 8 */
    v4f_default(&parts); parts.variant_size += 8;            expect_reject(TL4_E_V4_END, "R-V4-4 trailing 8 bytes");
    v4f_default(&parts); parts.variant_size -= 4;            expect_reject(TL4_E_V4_END, "R-V4-4 last record cut");
    /* FONT record: logical size must be 92; a FONT with inline data is a size error */
    v4f_default(&parts); put32(parts.variant + 3 * 40 + 28, 90); expect_reject(TL4_E_V4_SIZE, "R-V4-6 FONT logical size");
    v4f_default(&parts); put32(parts.variant + 3 * 40 + 20, 0); put32(parts.variant + 3 * 40 + 28, 0); expect_reject(TL4_E_V4_SIZE, "R-V4-6 FONT inline");
    /* INTLIST count over 402 and count/size mismatch; STRING without NUL */
    {
        uint8_t big[4] = {0x93, 0x01, 0, 0}; /* 403 */
        uint32_t off = 0;
        v4f_default(&parts);
        off = v4b_record(parts.variant, sizeof(parts.variant), 0, 6000, 211, 3, 0, 0, 0, big, 4);
        parts.variant_size = off; expect_reject(TL4_E_V4_SIZE, "R-V4-6 INTLIST count 403");
        big[0] = 1; v4f_default(&parts);
        off = v4b_record(parts.variant, sizeof(parts.variant), 0, 6000, 211, 3, 0, 0, 0, big, 4);     /* count 1 but only 4 bytes */
        parts.variant_size = off; expect_reject(TL4_E_V4_SIZE, "R-V4-6 INTLIST short");
        v4f_default(&parts);
        off = v4b_record(parts.variant, sizeof(parts.variant), 0, 601, 201, 3, 0, 0, 0, "AB", 2);     /* STRING "A" without NUL */
        parts.variant_size = off; expect_reject(TL4_E_V4_SIZE, "R-V4-6 STRING no NUL");
    }
}

static void test_drops_and_references(void)
{
    tl4_v4_stats s;
    uint32_t off;
    /* unknown type 241 with inline data is skipped and counted; a COLOR with reference != 0 is dropped and counted */
    v4f_default(&parts);
    off = v4b_record(parts.variant, sizeof(parts.variant), parts.variant_size, 5111, 241, 3, 0, 0, 0, "\x11\x22\x33\x44", 4);
    off = v4b_record(parts.variant, sizeof(parts.variant), off, 3806, 204, 3, 0, 0, 77, 0, 4);
    parts.variant_size = off;
    CHECK_STATUS(run(&s), TL4_OK);
    CHECK(s.variant.records == 14 && s.variant.dropped_unknown_type == 1 && s.variant.dropped_ref_on_value == 1);
    /* an unresolved FONT rejects (R-V4-12) */
    v4f_default(&parts); parts.omit_strings = 1; expect_reject(TL4_E_V4_REFERENCE, "R-V4-12 FONT string missing");
    v4f_default(&parts); put32(parts.variant + 3 * 40 + 20, 502); expect_reject(TL4_E_V4_REFERENCE, "R-V4-12 FONT string 502 empty");
    /* one unresolved IMAGE of 15 records is over 1% */
    v4f_default(&parts); parts.omit_image_509 = 1; expect_reject(TL4_E_V4_REFERENCE, "R-V4-12 IMAGE 509 missing");
    /* a reference above 16 bits never resolves */
    v4f_default(&parts); put32(parts.variant + 6 * 40 + 20, 0x10000); expect_reject(TL4_E_V4_REFERENCE, "R-V4-7 reference above 16 bits");
    /* the strings may come from a separately opened module (R-RES-1) */
    {
        tl4_pe style, mui;
        static uint8_t mui_image[8 * 1024];
        pb_res sres[1];
        pb_options o = {0, 0, 0, 0};
        size_t n, m;
        v4f_default(&parts); parts.omit_strings = 1;
        n = v4f_build(&parts, image, sizeof(image)); CHECK(n > 0);
        sres[0].type_name = 0; sres[0].type_id = 6; sres[0].name_name = 0; sres[0].name_id = 32; sres[0].language = 1033; sres[0].data = parts.strings; sres[0].size = parts.strings_size;
        m = pb_build(sres, 1, &o, mui_image, sizeof(mui_image), 0); CHECK(m > 0);
        CHECK_STATUS(tl4_pe_open(image, n, &style), TL4_OK);
        CHECK_STATUS(tl4_pe_open(mui_image, m, &mui), TL4_OK);
        CHECK_STATUS(tl4_v4_validate(&style, &mui, &s), TL4_OK);
        CHECK(s.fonts == 1 && s.unresolved_fonts == 0 && s.string_blocks == 0);
    }
    /* string lookup edge cases */
    {
        tl4_pe pe; const uint8_t *s16; uint32_t chars;
        size_t n;
        v4f_default(&parts); n = v4f_build(&parts, image, sizeof(image));
        CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_OK);
        CHECK_STATUS(tl4_v4_find_string(&pe, 501, &s16, &chars), TL4_OK); CHECK(chars == 30 && s16[0] == 'S');
        CHECK_STATUS(tl4_v4_find_string(&pe, 500, &s16, &chars), TL4_E_NOT_FOUND);  /* empty slot */
        CHECK_STATUS(tl4_v4_find_string(&pe, 9000, &s16, &chars), TL4_E_NOT_FOUND); /* no block */
        CHECK_STATUS(tl4_v4_find_string(&pe, 0x10000, &s16, &chars), TL4_E_ARG);
    }
}

/* R-V4-7 as amended: a STRING record may carry a string-table reference, as the RMAP documentation strings do */
static void test_string_reference(void)
{
    tl4_v4_stats s;
    uint32_t off;
    v4f_default(&parts);
    off = v4b_record(parts.rmap, sizeof(parts.rmap), parts.rmap_size, 601, 201, 0, 0, 0, 501, 0, 22);   /* -> the font string */
    parts.rmap_size = off;
    CHECK_STATUS(run(&s), TL4_OK);
    CHECK(s.rmap.records == 2 && s.strings == 1 && s.unresolved_strings == 0);
    v4f_default(&parts);
    off = v4b_record(parts.rmap, sizeof(parts.rmap), parts.rmap_size, 601, 201, 0, 0, 0, 502, 0, 22);   /* -> an empty slot */
    parts.rmap_size = off;
    expect_reject(TL4_E_V4_REFERENCE, "R-V4-12 one unresolved string of 16 records is over 1%");
}

static void test_missing_and_args(void)
{
    tl4_v4_stats s;
    tl4_pe pe;
    size_t n;
    v4f_default(&parts); parts.variant_size = 0; n = v4f_build(&parts, image, sizeof(image)); CHECK(n > 0);
    CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_OK);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &s), TL4_OK);                /* an empty VARIANT is a valid, empty style */
    CHECK(s.variant.records == 0);
    CHECK_STATUS(tl4_v4_validate(NULL, 0, &s), TL4_E_ARG);
    CHECK_STATUS(tl4_v4_validate(&pe, 0, NULL), TL4_E_ARG);
    pe.base = NULL;
    CHECK_STATUS(tl4_v4_validate(&pe, 0, &s), TL4_E_ARG);
    CHECK(s.cmap_names == 0);
}

int main(void)
{
    test_good();
    test_version();
    test_cmap();
    test_vmap();
    test_bcmap();
    test_records();
    test_drops_and_references();
    test_string_reference();
    test_missing_and_args();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
