/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Unit tests of the bounded PE / resource reader (REQUIREMENTS section 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <tl4/loader/pe.h>
#include "pebuild.h"

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

static uint8_t image[64 * 1024];
static uint8_t copy_[64 * 1024];

static const uint8_t version4[2] = {4, 0};
static const uint8_t png_like[16] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
static const uint8_t cmap[] = {'W', 0, 'i', 0, 'n', 0, 'd', 0, 'o', 0, 'w', 0, 0, 0, 0, 0};

/* A style-shaped resource set, given in a deliberately unsorted order: the builder sorts it as the format requires.
 * Types: CMAP, IMAGE, PACKTHEM_VERSION, VARIANT (names) then #6 (id); names under IMAGE: 508, 509, 510. */
static const pb_res style_res[] = {
    {"VARIANT", 0, "NORMAL", 0, 0, cmap, 8},
    {"IMAGE", 0, NULL, 509, 0, png_like, 12},
    {"PACKTHEM_VERSION", 0, NULL, 1, 0, version4, 2},
    {NULL, 6, NULL, 32, 1033, cmap, sizeof(cmap)},
    {"IMAGE", 0, NULL, 508, 0, png_like, sizeof(png_like)},
    {"CMAP", 0, "CMAP", 0, 0, cmap, sizeof(cmap)},
    {"IMAGE", 0, NULL, 510, 1033, png_like, 4},
};
#define STYLE_COUNT (sizeof(style_res) / sizeof(style_res[0]))

static size_t build_style_like(const pb_options *opt, pb_layout *layout)
{
    return pb_build(style_res, STYLE_COUNT, opt, image, sizeof(image), layout);
}

struct enum_ctx { int calls; int images; int stop_at; int ordered; const tl4_resource *prev; tl4_resource last; };

static int enum_cb(const tl4_resource *r, void *ctx)
{
    struct enum_ctx *c = (struct enum_ctx *)ctx;
    tl4_res_name img = tl4_res_ascii("image");
    c->calls++;
    if (tl4_res_name_equal(&r->type, &img)) c->images++;
    c->last = *r;
    return c->stop_at && c->calls == c->stop_at ? 7 : 0;
}

static void test_good_image(int pe32plus)
{
    pb_options o = {pe32plus, 0, 0, 0};
    pb_layout lay;
    tl4_pe pe;
    tl4_resource r;
    tl4_res_name t, n;
    struct enum_ctx ctx = {0, 0, 0, 1, 0, {{0}, {0}, 0, 0, 0, 0, 0}};
    int stopped = -1;
    size_t n_bytes = build_style_like(&o, &lay);

    CHECK(n_bytes > 0);
    CHECK_STATUS(tl4_pe_open(image, n_bytes, &pe), TL4_OK);
    CHECK(pe.is_pe32plus == (pe32plus ? 1 : 0));
    CHECK(pe.nresources == 7);
    CHECK(pe.ndirectories == 1 + 5 + 7);   /* root + 5 types + 7 names */
    CHECK(pe.nentries == 5 + 7 + 7);
    CHECK(pe.rsrc_off == lay.rsrc_raw);

    CHECK_STATUS(tl4_pe_enum(&pe, enum_cb, &ctx, &stopped), TL4_OK);
    CHECK(ctx.calls == 7 && ctx.images == 3 && stopped == 0);
    CHECK(ctx.last.type.kind == TL4_NAME_ID && ctx.last.type.id == 6);   /* the id type comes after the names */
    ctx.calls = 0; ctx.stop_at = 2;
    CHECK_STATUS(tl4_pe_enum(&pe, enum_cb, &ctx, &stopped), TL4_OK);
    CHECK(ctx.calls == 2 && stopped == 7);

    t = tl4_res_ascii("packthem_version"); n = tl4_res_id(1);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
    CHECK(r.size == 2 && r.data[0] == 4 && r.data[1] == 0 && r.language == 0);
    CHECK(r.data == image + r.offset && r.offset >= lay.rsrc_raw && r.offset + r.size <= lay.total);

    t = tl4_res_ascii("IMAGE"); n = tl4_res_id(509);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
    CHECK(r.size == 12 && r.language == 0 && memcmp(r.data, png_like, 12) == 0);
    {
        uint32_t cnt = 99;
        CHECK_STATUS(tl4_pe_count_type(&pe, &t, &cnt), TL4_OK);
        CHECK(cnt == 3);
    }

    t = tl4_res_ascii("variant"); n = tl4_res_ascii("normal");
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
    CHECK(r.size == 8 && r.name.kind == TL4_NAME_UTF16 && r.name.length == 6);

    t = tl4_res_id(6); n = tl4_res_id(32);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
    CHECK(r.language == 1033 && r.size == sizeof(cmap));

    t = tl4_res_ascii("IMAGE"); n = tl4_res_id(511);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_E_NOT_FOUND);
    t = tl4_res_ascii("BCMAP"); n = tl4_res_ascii("BCMAP");
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_E_NOT_FOUND);
    {
        uint32_t cnt = 99;
        CHECK_STATUS(tl4_pe_count_type(&pe, &t, &cnt), TL4_OK);
        CHECK(cnt == 0);
        CHECK_STATUS(tl4_pe_count_type(&pe, &t, NULL), TL4_E_ARG);
    }
    t = tl4_res_id(6); n = tl4_res_ascii("IMAGE");         /* an id never equals a string */
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_E_NOT_FOUND);
}

static void test_truncation(void)
{
    pb_options o = {1, 0, 0, 0};
    pb_layout lay;
    tl4_pe pe;
    size_t n = build_style_like(&o, &lay), cut;
    int ok_count = 0;

    CHECK(n > 0);
    /* every prefix of the image must be rejected; none may crash */
    for (cut = 0; cut < n; cut += (cut < 0x300 ? 1 : 37))
        if (tl4_pe_open(image, cut, &pe) == TL4_OK) ok_count++;
    CHECK(ok_count == 0);
    CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_OK);
    CHECK_STATUS(tl4_pe_open(image, 63, &pe), TL4_E_TRUNCATED);
    CHECK_STATUS(tl4_pe_open(NULL, n, &pe), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_open(image, (size_t)TL4_PE_MAX_FILE_SIZE + 1, &pe), TL4_E_TOO_LARGE);
    CHECK_STATUS(tl4_pe_open(image, n, NULL), TL4_E_ARG);
}

/* R-GEN-5: a failed open leaves a cleared handle; nothing can be looked up through it */
static void test_failed_open_clears(void)
{
    pb_options o = {1, 0, 0, 0};
    pb_layout lay;
    tl4_pe pe;
    tl4_resource r;
    tl4_res_name t = tl4_res_ascii("IMAGE"), nm = tl4_res_id(508);
    uint32_t cnt = 5;
    int stopped = 3;
    size_t n = build_style_like(&o, &lay);

    CHECK(n > 0);
    CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_OK);
    CHECK(pe.base == image && pe.nresources == 7);
    /* a failure late in the walk (data entry outside the section) must wipe everything the headers had set */
    memcpy(copy_, image, n);
    pb_put32(copy_ + lay.first_data_entry + 4, lay.rsrc_raw_size);
    CHECK_STATUS(tl4_pe_open(copy_, n, &pe), TL4_E_RSRC_DATA);
    CHECK(pe.base == NULL && pe.size == 0 && pe.rsrc_off == 0 && pe.rsrc_limit == 0 && pe.nsections == 0);
    CHECK(pe.nresources == 0 && pe.nentries == 0 && pe.ndirectories == 0);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &nm, &r), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_enum(&pe, enum_cb, &cnt, &stopped), TL4_E_ARG);
    CHECK_STATUS(tl4_pe_count_type(&pe, &t, &cnt), TL4_E_ARG);
    /* an early failure too */
    memcpy(copy_, image, n);
    copy_[0] = 'X';
    pe.base = image;
    CHECK_STATUS(tl4_pe_open(copy_, n, &pe), TL4_E_DOS_HEADER);
    CHECK(pe.base == NULL);
}

static void corrupt32(size_t n, uint32_t off, uint32_t value, tl4_status want)
{
    tl4_pe pe;
    memcpy(copy_, image, n);
    pb_put32(copy_ + off, value);
    CHECK_STATUS(tl4_pe_open(copy_, n, &pe), want);
}

static void corrupt16(size_t n, uint32_t off, uint32_t value, tl4_status want)
{
    tl4_pe pe;
    memcpy(copy_, image, n);
    pb_put16(copy_ + off, value);
    CHECK_STATUS(tl4_pe_open(copy_, n, &pe), want);
}

/* One corruption per rejection path (R-PROC-4). */
static void test_corruptions(void)
{
    pb_options o = {1, 0, 0, 0};
    pb_layout lay;
    size_t n = build_style_like(&o, &lay);
    uint32_t root_entry_off, type_dir_rel, first_name_entry, first_lang_entry, name_rel;
    tl4_pe pe;

    CHECK(n > 0);
    CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_OK);

    /* R-PE-2: DOS / PE */
    memcpy(copy_, image, n); copy_[0] = 'X'; CHECK_STATUS(tl4_pe_open(copy_, n, &pe), TL4_E_DOS_HEADER);
    corrupt32(n, 0x3C, 0xFFFFFFF0u, TL4_E_DOS_HEADER);
    corrupt32(n, 0x3C, 8, TL4_E_DOS_HEADER);
    corrupt32(n, lay.e_lfanew, 0x00004550u ^ 1u, TL4_E_PE_SIGNATURE);
    /* R-PE-3: file header: zero sections, 33 sections, optional header beyond the file, bad magic */
    corrupt16(n, lay.file_header + 2, 0, TL4_E_FILE_HEADER);
    corrupt16(n, lay.file_header + 2, 33, TL4_E_FILE_HEADER);
    corrupt16(n, lay.file_header + 16, 0xFFF0, TL4_E_TRUNCATED);
    corrupt16(n, lay.optional_header, 0x107, TL4_E_OPTIONAL_HEADER);
    /* NumberOfRvaAndSizes without a resource entry, and absurd */
    corrupt32(n, lay.optional_header + 108, 2, TL4_E_NO_RESOURCES);
    corrupt32(n, lay.optional_header + 108, 17, TL4_E_OPTIONAL_HEADER);
    /* R-PE-5: resource directory RVA in no section; size zero; size beyond the section */
    corrupt32(n, lay.data_directory + 16, 0x90000, TL4_E_NO_RESOURCES);
    corrupt32(n, lay.data_directory + 16 + 4, 0, TL4_E_NO_RESOURCES);
    corrupt32(n, lay.data_directory + 16 + 4, lay.rsrc_raw_size + 1, TL4_E_NO_RESOURCES);
    /* R-PE-4: section raw data beyond the file; raw pointer beyond the file */
    corrupt32(n, lay.section_header + 16, 0x7FFFFFF0u, TL4_E_SECTION);
    corrupt32(n, lay.section_header + 20, (uint32_t)n, TL4_E_SECTION);

    /* the tree: the first root entry is the first named type (CMAP) */
    root_entry_off = lay.root_entries;
    type_dir_rel = pb_get32(image + root_entry_off + 4) & 0x7FFFFFFFu;
    name_rel = pb_get32(image + root_entry_off) & 0x7FFFFFFFu;
    first_name_entry = lay.rsrc_raw + type_dir_rel + 16;
    first_lang_entry = lay.rsrc_raw + (pb_get32(image + first_name_entry + 4) & 0x7FFFFFFFu) + 16;

    /* R-PE-7: root entry count 0, over the per-directory limit, and more than fits in the section */
    corrupt32(n, lay.rsrc_raw + 12, 0, TL4_E_RSRC_DIRECTORY);          /* both counts zero */
    corrupt16(n, lay.rsrc_raw + 14, 20000, TL4_E_RSRC_DIRECTORY);
    corrupt16(n, lay.rsrc_raw + 14, 4000, TL4_E_RSRC_DIRECTORY);
    /* R-PE-6: a type entry that is data; a type entry pointing at the root (cycle) */
    corrupt32(n, root_entry_off + 4, type_dir_rel, TL4_E_RSRC_DIRECTORY);
    corrupt32(n, root_entry_off + 4, 0x80000000u, TL4_E_RSRC_ENTRY);
    /* R-PE-5: a type directory outside the section */
    corrupt32(n, root_entry_off + 4, 0x80000000u | (lay.rsrc_raw_size - 4), TL4_E_RSRC_DIRECTORY);
    corrupt32(n, root_entry_off + 4, 0x80000000u | 0x7FFFFFF0u, TL4_E_RSRC_DIRECTORY);
    /* R-PE-7: a string name outside the section; length 0; length 65; an id above 16 bits */
    corrupt32(n, root_entry_off, 0x80000000u | (lay.rsrc_raw_size - 1), TL4_E_RSRC_NAME);
    corrupt16(n, lay.rsrc_raw + name_rel, 0, TL4_E_RSRC_NAME);
    corrupt16(n, lay.rsrc_raw + name_rel, 65, TL4_E_RSRC_NAME);
    corrupt32(n, root_entry_off, 0x10000, TL4_E_RSRC_NAME);
    /* R-PE-8: a name after an id at the root (turn the first named type into an id while named ones follow) */
    corrupt32(n, root_entry_off, 5, TL4_E_RSRC_ORDER);
    /* R-PE-8: the header split: 4 names + 1 id is the truth; claim 5 named / 0 ids, then 3 named / 2 ids */
    corrupt32(n, lay.rsrc_raw + 12, 5u, TL4_E_RSRC_ORDER);
    corrupt32(n, lay.rsrc_raw + 12, 3u | (2u << 16), TL4_E_RSRC_ORDER);
    /* R-PE-8: two equal ids under IMAGE (509 renamed to 508) and a descending pair (510 renamed to 1) */
    {
        uint32_t image_entry = 0, k;
        for (k = 0; k < 5; k++)
        {
            uint32_t nm = pb_get32(image + root_entry_off + 8 * k);
            if (nm & 0x80000000u)
            {
                uint32_t rel = nm & 0x7FFFFFFFu;
                if (image[lay.rsrc_raw + rel] == 5 && image[lay.rsrc_raw + rel + 2] == 'I') image_entry = root_entry_off + 8 * k;
            }
        }
        CHECK(image_entry != 0);
        if (image_entry)
        {
            uint32_t idir = lay.rsrc_raw + (pb_get32(image + image_entry + 4) & 0x7FFFFFFFu);
            CHECK(pb_get32(image + idir + 16) == 508 && pb_get32(image + idir + 24) == 509 && pb_get32(image + idir + 32) == 510);
            corrupt32(n, idir + 24, 508, TL4_E_RSRC_ORDER);
            corrupt32(n, idir + 32, 1, TL4_E_RSRC_ORDER);
            /* R-PE-8: a second language entry under 508 (the directory claims two entries) */
            {
                uint32_t ldir = lay.rsrc_raw + (pb_get32(image + idir + 20) & 0x7FFFFFFFu);
                corrupt16(n, ldir + 14, 2, TL4_E_RSRC_LANGUAGES);
            }
        }
    }
    /* R-PE-6: a name entry that is data; a name directory equal to its parent (cycle) */
    corrupt32(n, first_name_entry + 4, 16, TL4_E_RSRC_DIRECTORY);
    corrupt32(n, first_name_entry + 4, 0x80000000u | type_dir_rel, TL4_E_RSRC_ENTRY);
    /* R-PE-6: a language entry that is a directory (depth 4); a language with a string name */
    corrupt32(n, first_lang_entry + 4, 0x80000000u | type_dir_rel, TL4_E_RSRC_DIRECTORY);
    corrupt32(n, first_lang_entry, 0x80000000u | 16u, TL4_E_RSRC_NAME);
    /* R-PE-9: data entry outside the section; data beyond the section's raw data; data below the section */
    corrupt32(n, first_lang_entry + 4, lay.rsrc_raw_size - 8, TL4_E_RSRC_DATA);
    corrupt32(n, lay.first_data_entry + 4, lay.rsrc_raw_size, TL4_E_RSRC_DATA);
    corrupt32(n, lay.first_data_entry, lay.rsrc_va - 1, TL4_E_RSRC_DATA);
    corrupt32(n, lay.first_data_entry, 0xFFFFFFF0u, TL4_E_RSRC_DATA);
    /* the raw size shrunk below the tree: the walk must fail, not read past the declared data */
    corrupt32(n, lay.section_header + 16, 0x40, TL4_E_NO_RESOURCES);
}

/* R-PE-8 through the builder: unsorted input kept as is must be rejected for order. */
static void test_unsorted_builder(void)
{
    pb_options o = {1, 0, 0, 1};
    pb_layout lay;
    tl4_pe pe;
    size_t n = build_style_like(&o, &lay);
    CHECK(n > 0);
    CHECK_STATUS(tl4_pe_open(image, n, &pe), TL4_E_RSRC_ORDER);
}

static void test_variants(void)
{
    pb_options o = {0, 0x4000, 3, 0};
    pb_layout lay;
    tl4_pe pe;
    tl4_resource r;
    tl4_res_name t = tl4_res_ascii("IMAGE"), n = tl4_res_id(508);
    size_t bytes = build_style_like(&o, &lay);
    CHECK(bytes > 0);
    CHECK_STATUS(tl4_pe_open(image, bytes, &pe), TL4_OK);
    CHECK(pe.nsections == 4 && pe.is_pe32plus == 0 && pe.rsrc_section_va == 0x4000);
    CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
    CHECK(r.size == sizeof(png_like) && r.rva >= 0x4000 && r.offset + r.size <= bytes);
}

static void test_names(void)
{
    tl4_res_name a = tl4_res_ascii("Image"), b = tl4_res_ascii("IMAGE"), c = tl4_res_ascii("IMAGES"), d = tl4_res_id(2);
    uint8_t u[] = {'i', 0, 'm', 0, 'a', 0, 'g', 0, 'e', 0};
    tl4_res_name e = {0};
    e.kind = TL4_NAME_UTF16; e.length = 5; e.utf16 = u;
    CHECK(tl4_res_name_equal(&a, &b));
    CHECK(!tl4_res_name_equal(&a, &c));
    CHECK(!tl4_res_name_equal(&a, &d));
    CHECK(tl4_res_name_equal(&a, &e) && tl4_res_name_equal(&e, &b));
    CHECK(!tl4_res_name_equal(&c, &e));
    CHECK(!tl4_res_name_equal(&d, &e));
    CHECK(tl4_res_name_equal(&d, &d));
    {
        /* a caller string longer than any valid name, and an empty one, match nothing (not even themselves) */
        char longname[TL4_PE_MAX_NAME_CHARS + 2];
        tl4_res_name l, empty = tl4_res_ascii("");
        memset(longname, 'a', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = 0;
        l = tl4_res_ascii(longname);
        CHECK(l.length == 0 && l.ascii == NULL);
        CHECK(!tl4_res_name_equal(&l, &l) && !tl4_res_name_equal(&l, &a) && !tl4_res_name_equal(&empty, &empty));
        longname[TL4_PE_MAX_NAME_CHARS] = 0;      /* exactly the limit is a valid name */
        l = tl4_res_ascii(longname);
        CHECK(l.length == TL4_PE_MAX_NAME_CHARS && tl4_res_name_equal(&l, &l));
    }
}

int main(void)
{
    test_names();
    test_good_image(1);
    test_good_image(0);
    test_truncation();
    test_failed_open_clears();
    test_corruptions();
    test_unsorted_builder();
    test_variants();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
