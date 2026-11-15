/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder: PNG byte streams with correct chunk CRCs, built in memory; nothing decodes them, so the
 *              IDAT contents are arbitrary bytes (R-PROC-1: every test input is generated)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#ifndef TL4_LOADER_PNGBUILD_H
#define TL4_LOADER_PNGBUILD_H

#include <stddef.h>
#include <stdint.h>

/* Start a stream: the signature. Returns the bytes written (8), 0 when cap is too small. */
uint32_t pngb_begin(uint8_t *out, uint32_t cap);
/* Append one chunk (length, type, data, CRC). Returns the new offset, 0 on error. */
uint32_t pngb_chunk(uint8_t *out, uint32_t cap, uint32_t off, const char type[4], const uint8_t *data, uint32_t len);
/* Append an IHDR. */
uint32_t pngb_ihdr(uint8_t *out, uint32_t cap, uint32_t off, uint32_t width, uint32_t height, uint8_t depth, uint8_t colour,
                   uint8_t interlace);
/* A whole image in the corpus' most common order: IHDR, gAMA, pHYs, [sRGB], [tEXt], IDAT x n, IEND. `idat_total` bytes
 * of filler are split into `nidat` chunks. Returns the size, 0 on error. */
uint32_t pngb_image(uint8_t *out, uint32_t cap, uint32_t width, uint32_t height, uint8_t colour, uint32_t idat_total,
                    uint32_t nidat, int with_srgb, int with_text);
/* The CRC the builder uses, for tests that corrupt one. */
uint32_t pngb_crc(const uint8_t *p, uint32_t n);
/* Big-endian store. */
void pngb_put32(uint8_t *p, uint32_t v);

#endif
