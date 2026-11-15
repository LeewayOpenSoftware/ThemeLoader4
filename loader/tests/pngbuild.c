/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder: PNG byte streams with correct chunk CRCs
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include "pngbuild.h"
#include <string.h>

void pngb_put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* bitwise CRC-32: slow and obviously right, which is what a fixture wants */
uint32_t pngb_crc(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++)
    {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
    return crc ^ 0xFFFFFFFFu;
}

uint32_t pngb_begin(uint8_t *out, uint32_t cap)
{
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 13, 10, 26, 10 };
    if (cap < 8) return 0;
    memcpy(out, sig, 8);
    return 8;
}

uint32_t pngb_chunk(uint8_t *out, uint32_t cap, uint32_t off, const char type[4], const uint8_t *data, uint32_t len)
{
    if (off == 0 || (uint64_t)off + 12u + len > cap) return 0;
    pngb_put32(out + off, len);
    memcpy(out + off + 4, type, 4);
    if (len) { if (data) memcpy(out + off + 8, data, len); else memset(out + off + 8, 0x5A, len); }
    pngb_put32(out + off + 8 + len, pngb_crc(out + off + 4, 4u + len));
    return off + 12u + len;
}

uint32_t pngb_ihdr(uint8_t *out, uint32_t cap, uint32_t off, uint32_t width, uint32_t height, uint8_t depth, uint8_t colour,
                   uint8_t interlace)
{
    uint8_t ihdr[13];
    pngb_put32(ihdr, width); pngb_put32(ihdr + 4, height);
    ihdr[8] = depth; ihdr[9] = colour; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = interlace;
    return pngb_chunk(out, cap, off, "IHDR", ihdr, 13);
}

uint32_t pngb_image(uint8_t *out, uint32_t cap, uint32_t width, uint32_t height, uint8_t colour, uint32_t idat_total,
                    uint32_t nidat, int with_srgb, int with_text)
{
    static const uint8_t gama[4] = { 0, 0, 0xB1, 0x8F };            /* 45455 */
    static const uint8_t phys[9] = { 0, 0, 0x0B, 0x13, 0, 0, 0x0B, 0x13, 1 };
    static const uint8_t srgb[1] = { 0 };
    static const uint8_t text[] = { 'T', 'i', 't', 'l', 'e', 0, 'g', 'e', 'n', 'e', 'r', 'a', 't', 'e', 'd' };
    uint32_t off = pngb_begin(out, cap), i, each, rest;
    off = pngb_ihdr(out, cap, off, width, height, 8, colour, 0);
    if (with_srgb) off = pngb_chunk(out, cap, off, "sRGB", srgb, 1);
    off = pngb_chunk(out, cap, off, "gAMA", gama, 4);
    off = pngb_chunk(out, cap, off, "pHYs", phys, 9);
    if (with_text) off = pngb_chunk(out, cap, off, "tEXt", text, (uint32_t)sizeof(text));
    if (nidat == 0) nidat = 1;
    each = idat_total / nidat; rest = idat_total - each * (nidat - 1);
    for (i = 0; i < nidat; i++) off = pngb_chunk(out, cap, off, "IDAT", 0, i + 1 < nidat ? each : rest);
    off = pngb_chunk(out, cap, off, "IEND", 0, 0);
    return off;
}
