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
#include <string.h>
#include <tl4/loader/pe.h>

static int touch_cb(const tl4_resource *r, void *ctx)
{
    /* read the first and last byte of every resource: the reader promised they are inside the image */
    volatile uint8_t sink = 0;
    (void)ctx;
    if (r->size)
    {
        sink = r->data[0];
        sink = r->data[r->size - 1];
    }
    if (r->size) sink = r->data[r->offset - r->offset]; /* data == base + offset by contract */
    if (r->type.kind == TL4_NAME_UTF16) sink = r->type.utf16[2 * r->type.length - 1];
    if (r->name.kind == TL4_NAME_UTF16) sink = r->name.utf16[2 * r->name.length - 1];
    (void)sink;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_pe pe;
    tl4_resource r;
    tl4_res_name types[] = {tl4_res_ascii("IMAGE"), tl4_res_ascii("VARIANT"), tl4_res_ascii("CMAP"),
                            tl4_res_ascii("PACKTHEM_VERSION"), tl4_res_id(6)};
    tl4_res_name names[] = {tl4_res_id(1), tl4_res_id(508), tl4_res_ascii("NORMAL"), tl4_res_ascii("CMAP")};
    size_t i, j;
    int stopped;

    if (tl4_pe_open(data, size, &pe) != TL4_OK) return 0;
    (void)tl4_pe_enum(&pe, touch_cb, 0, &stopped);
    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++)
    {
        (void)tl4_pe_count_type(&pe, &types[i]);
        for (j = 0; j < sizeof(names) / sizeof(names[0]); j++)
            if (tl4_pe_find(&pe, &types[i], &names[j], &r) == TL4_OK) touch_cb(&r, 0);
    }
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
