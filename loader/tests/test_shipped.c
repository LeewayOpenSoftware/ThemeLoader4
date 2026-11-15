/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The shipped version-3 style of the system under the amended R-PE-8 (2026-11-15): it opens, its
 *              PACKTHEM_VERSION is 3 with one language entry, and its multi-language pairs are counted
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The file is read from the path in TL4_SHIPPED_STYLE at run time and never enters the repository (R-PROC-1).
 * Exit 77 = skipped when the variable is unset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/pe.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)

struct shipped_ctx {
    const tl4_pe *pe;
    uint32_t rows, pairs, multi_pairs, max_languages;
    uint32_t string_pairs, string_langs_min, string_langs_max;   /* RT_STRING (#6): one table per language */
};

static int count_cb(const tl4_resource *r, void *ctx)
{
    struct shipped_ctx *c = (struct shipped_ctx *)ctx;
    tl4_resource low;
    c->rows++;
    if (r->nlanguages > c->max_languages) c->max_languages = r->nlanguages;
    /* a pair is counted once, on the entry the lookup returns (the lowest id) */
    if (tl4_pe_find(c->pe, &r->type, &r->name, &low) == TL4_OK && low.language == r->language)
    {
        c->pairs++;
        if (r->nlanguages > 1) c->multi_pairs++;
        if (r->type.kind == TL4_NAME_ID && r->type.id == 6)
        {
            c->string_pairs++;
            if (c->string_pairs == 1 || r->nlanguages < c->string_langs_min) c->string_langs_min = r->nlanguages;
            if (r->nlanguages > c->string_langs_max) c->string_langs_max = r->nlanguages;
        }
    }
    return 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;
    uint8_t *buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    if ((unsigned long)len > TL4_PE_MAX_FILE_SIZE) { fclose(f); return 0; }
    buf = (uint8_t *)malloc((size_t)len);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 0; }
    fclose(f);
    *size = (size_t)len;
    return buf;
}

int main(void)
{
    const char *path = getenv("TL4_SHIPPED_STYLE");
    size_t size = 0;
    uint8_t *data;
    tl4_pe pe;
    tl4_resource r;
    tl4_res_name t = tl4_res_ascii("PACKTHEM_VERSION"), n = tl4_res_id(1);
    struct shipped_ctx ctx;
    int stopped = -1;

    if (!path || !*path)
    {
        printf("tl4_loader_test_shipped: TL4_SHIPPED_STYLE unset, skipped\n");
        return 77;
    }
    data = read_file(path, &size);
    if (!data) { printf("FAIL: cannot read %s\n", path); return 1; }

    CHECK_STATUS(tl4_pe_open(data, size, &pe), TL4_OK);
    if (pe.base)
    {
        CHECK_STATUS(tl4_pe_find(&pe, &t, &n, &r), TL4_OK);
        CHECK(r.size == 2 && r.data[0] == 3 && r.data[1] == 0);     /* version 3 */
        CHECK(r.nlanguages == 1);                                    /* R-PE-8: the routing resource has one entry */

        memset(&ctx, 0, sizeof(ctx));
        ctx.pe = &pe;
        CHECK_STATUS(tl4_pe_enum(&pe, count_cb, &ctx, &stopped), TL4_OK);
        CHECK(stopped == 0 && ctx.rows == pe.nresources);
        CHECK(ctx.multi_pairs == pe.nmulti_pairs);
        CHECK(ctx.max_languages <= TL4_PE_MAX_LANGUAGES);
        printf("shipped style: %lu bytes, %u data entries, %u pairs, %u pairs with several languages (max %u), "
               "%u string tables with %u..%u languages, version %u\n",
               (unsigned long)size, pe.nresources, ctx.pairs, pe.nmulti_pairs, ctx.max_languages,
               ctx.string_pairs, ctx.string_langs_min, ctx.string_langs_max, r.size == 2 ? (unsigned)r.data[0] : 0u);
    }
    free(data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
