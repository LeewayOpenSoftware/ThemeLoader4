/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness: a PNG resource through tl4_png_check and tl4_png_idat_enum (S2: fuzz_png)
 *
 * Two input forms, chosen by the first byte: raw bytes (the rest of the input is the resource), or structured: the rest
 * is a list of (type[4], length[2] big-endian, data) items from which the harness builds a PNG with the signature and
 * correct CRCs, so the campaign spends its time past the CRC check. The core has no CRC switch.
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/png.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

struct acc { uint32_t chunks, bytes, last_end, size; };
struct stopper { uint32_t calls, stop_at; };
static int stop_cb(uint32_t offset, uint32_t size, void *context)
{
    struct stopper *s = (struct stopper *)context;
    (void)offset; (void)size;
    s->calls++;
    return s->calls - 1 == s->stop_at ? 7 : 0;
}

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
           a->interlace == b->interlace && a->has_srgb == b->has_srgb && a->has_gama == b->has_gama &&
           a->has_phys == b->has_phys && a->has_text == b->has_text && a->chunks == b->chunks && a->idat_chunks == b->idat_chunks &&
           a->idat_bytes == b->idat_bytes && a->pixels == b->pixels;
}

static uint32_t crc32_bitwise(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) { crc ^= p[i]; for (k = 0; k < 8; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1; }
    return crc ^ 0xFFFFFFFFu;
}

static void put32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* Build a PNG from (type, len16, data) items: every chunk gets its CRC. Returns the size, 0 when it does not fit. */
static uint32_t build_structured(const uint8_t *items, size_t n, uint8_t *out, uint32_t cap)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    size_t i = 0;
    uint32_t off = 8;
    if (cap < 8) return 0;
    memcpy(out, sig, 8);
    while (i + 6 <= n)
    {
        uint32_t len = ((uint32_t)items[i + 4] << 8) | items[i + 5];
        if (len > n - (i + 6)) len = (uint32_t)(n - (i + 6));
        if ((uint64_t)off + 12u + len > cap) return 0;
        put32be(out + off, len);
        memcpy(out + off + 4, items + i, 4);
        memcpy(out + off + 8, items + i + 6, len);
        put32be(out + off + 8 + len, crc32_bitwise(out + off + 4, 4u + len));
        off += 12u + len;
        i += 6 + len;
    }
    return off;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    tl4_png_info info, again, zero;
    tl4_status st;
    struct acc a;
    int stopped = -1;
    uint8_t *copy;
    if (size < 1 || size > 0xFFFFFFFFu) return 0;
    memset(&zero, 0, sizeof(zero));
    if (data[0] & 1)
    {
        /* structured: the items become a PNG with correct CRCs */
        uint32_t cap = (uint32_t)size + 16u * 64u;
        copy = (uint8_t *)malloc(cap);
        if (!copy) return 0;
        size = build_structured(data + 1, size - 1, copy, cap);
        if (size == 0) { free(copy); return 0; }
    }
    else
    {
        size -= 1;
        copy = (uint8_t *)malloc(size ? size : 1);                          /* exact size: an over-read faults */
        if (!copy) return 0;
        memcpy(copy, data + 1, size);
    }

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
    PROMISE(info.chunks <= TL4_PNG_MAX_CHUNKS && info.idat_chunks >= 1 && info.idat_chunks <= TL4_PNG_MAX_IDAT_CHUNKS);
    PROMISE(info.idat_bytes <= TL4_PNG_MAX_COMPRESSED && info.idat_bytes <= size);
    a.chunks = 0; a.bytes = 0; a.last_end = 0; a.size = (uint32_t)size;
    PROMISE(tl4_png_idat_enum(copy, (uint32_t)size, idat_cb, &a, &stopped) == TL4_OK && stopped == 0);
    PROMISE(a.chunks == info.idat_chunks && a.bytes == info.idat_bytes);  /* enum agrees with check */
    PROMISE(info.idat_bytes >= 1);
    {
        /* a stop at an index taken from the input: TL4_OK (the check accepted the image), the calls end there */
        struct stopper sp;
        sp.calls = 0; sp.stop_at = data[0] >> 1;
        PROMISE(tl4_png_idat_enum(copy, (uint32_t)size, stop_cb, &sp, &stopped) == TL4_OK);
        if (sp.stop_at < info.idat_chunks) PROMISE(stopped == 7 && sp.calls == sp.stop_at + 1);
        else PROMISE(stopped == 0 && sp.calls == info.idat_chunks);
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
    printf("fuzz_png: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
