/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder for version-4 resources
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include "v4build.h"
#include <string.h>

static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v) { put16(p, v & 0xFFFFu); put16(p + 2, v >> 16); }

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

uint32_t v4b_cmap(uint8_t *out, uint32_t cap, const char *const *names, uint32_t count)
{
    uint32_t off = 0, i, c;
    for (i = 0; i < count; i++)
    {
        uint32_t len = slen(names[i]), bytes = ((2u * (len + 1) + 7u) & ~7u);
        if (off + bytes > cap) return 0;
        memset(out + off, 0, bytes);
        for (c = 0; c < len; c++) put16(out + off + 2 * c, (uint8_t)names[i][c]);
        off += bytes;
    }
    return off;
}

static uint32_t vmap_one(uint8_t *out, uint32_t cap, uint32_t off, const char *s)
{
    uint32_t len = slen(s), n = len + 1, bytes = (4u + 2u * n + 3u) & ~3u, c;
    if (off + bytes > cap) return 0;
    memset(out + off, 0, bytes);
    put32(out + off, n);
    for (c = 0; c < len; c++) put16(out + off + 4 + 2 * c, (uint8_t)s[c]);
    return off + bytes;
}

uint32_t v4b_vmap(uint8_t *out, uint32_t cap, const char *variant, const char *size, const char *color)
{
    uint32_t off = vmap_one(out, cap, 0, variant);
    if (off) off = vmap_one(out, cap, off, size);
    if (off) off = vmap_one(out, cap, off, color);
    return off;
}

uint32_t v4b_bcmap(uint8_t *out, uint32_t cap, const uint32_t *entries, uint32_t count)
{
    uint32_t i;
    if (4u + 4u * count > cap) return 0;
    put32(out, count);
    for (i = 0; i < count; i++) put32(out + 4 + 4 * i, entries[i]);
    return 4u + 4u * count;
}

uint32_t v4b_record(uint8_t *out, uint32_t cap, uint32_t off, uint32_t name_id, uint32_t type_id, uint32_t class_id,
                    uint32_t part_id, uint32_t state_id, uint32_t reference, const void *data, uint32_t cb)
{
    uint32_t total = 32u + (reference == 0 ? cb : 0u);
    total = (total + 7u) & ~7u;
    if (off + total > cap) return 0;
    memset(out + off, 0, total);
    put32(out + off, name_id);
    put32(out + off + 4, type_id);
    put32(out + off + 8, class_id);
    put32(out + off + 12, part_id);
    put32(out + off + 16, state_id);
    put32(out + off + 20, reference);
    put32(out + off + 28, cb);
    if (reference == 0 && cb && data) memcpy(out + off + 32, data, cb);
    return off + total;
}

uint32_t v4b_string_block(uint8_t *out, uint32_t cap, const char *const strings[16])
{
    uint32_t off = 0, i, c;
    for (i = 0; i < 16; i++)
    {
        uint32_t len = slen(strings[i]);
        if (off + 2 + 2 * len > cap) return 0;
        put16(out + off, len);
        for (c = 0; c < len; c++) put16(out + off + 2 + 2 * c, (uint8_t)strings[i][c]);
        off += 2 + 2 * len;
    }
    return off;
}
