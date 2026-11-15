/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness: a PNG resource through tl4_png_check and tl4_png_idat_enum (S2: fuzz_png)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/png.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

struct acc { uint32_t chunks, bytes, last_end, size; };

static int idat_cb(uint32_t offset, uint32_t size, void *context)
{
    struct acc *a = (struct acc *)context;
    PROMISE(offset <= a->size && size <= a->size - offset);
    PROMISE(offset >= a->last_end);                                       /* in file order */
    a->chunks++; a->bytes += size; a->last_end = offset + size;
    return 0;
}

static int infos_equal(const tl4_png_info *a, const tl4_png_info *b)
{
    return a->width == b->width && a->height == b->height && a->bit_depth == b->bit_depth && a->colour_type == b->colour_type &&
           a->interlace == b->interlace && a->has_trns == b->has_trns && a->has_srgb == b->has_srgb && a->has_gama == b->has_gama &&
           a->has_phys == b->has_phys && a->has_text == b->has_text && a->chunks == b->chunks && a->idat_chunks == b->idat_chunks &&
           a->idat_bytes == b->idat_bytes && a->pixels == b->pixels;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_png_info info, again, zero;
    tl4_status st;
    struct acc a;
    int stopped = -1;
    uint8_t *copy;
    if (size > 0xFFFFFFFFu) return 0;
    copy = (uint8_t *)malloc(size ? size : 1);                              /* exact size: an over-read faults */
    if (!copy) return 0;
    memcpy(copy, data, size);
    memset(&zero, 0, sizeof(zero));

    st = tl4_png_check(copy, (uint32_t)size, &info);
    PROMISE(tl4_png_check(copy, (uint32_t)size, &again) == st && infos_equal(&info, &again));   /* R-GEN-8 */
    if (st != TL4_OK)
    {
        PROMISE(infos_equal(&info, &zero));                                /* R-GEN-5 */
        free(copy);
        return 0;
    }
    PROMISE(info.width >= 1 && info.width <= TL4_PNG_MAX_SIDE && info.height >= 1 && info.height <= TL4_PNG_MAX_SIDE);
    PROMISE(info.pixels == (uint64_t)info.width * info.height && info.pixels <= TL4_PNG_MAX_PIXELS);
    PROMISE(info.bit_depth == 8 && (info.colour_type == 2 || info.colour_type == 6) && info.interlace == 0);
    PROMISE(!(info.has_trns && info.colour_type == 6));
    PROMISE(info.chunks <= TL4_PNG_MAX_CHUNKS && info.idat_chunks >= 1 && info.idat_chunks <= TL4_PNG_MAX_IDAT_CHUNKS);
    PROMISE(info.idat_bytes <= TL4_PNG_MAX_COMPRESSED && info.idat_bytes <= size);
    a.chunks = 0; a.bytes = 0; a.last_end = 0; a.size = (uint32_t)size;
    PROMISE(tl4_png_idat_enum(copy, (uint32_t)size, idat_cb, &a, &stopped) == TL4_OK && stopped == 0);
    PROMISE(a.chunks == info.idat_chunks && a.bytes == info.idat_bytes);  /* enum agrees with check */
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
    printf("fuzz_png: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
