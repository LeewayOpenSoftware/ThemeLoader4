/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder for version-4 resources (CMAP, VMAP, BCMAP, records, string blocks), all generated
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#ifndef TL4_LOADER_V4BUILD_H
#define TL4_LOADER_V4BUILD_H

#include <stddef.h>
#include <stdint.h>

/* Each builder writes into out[cap] and returns the bytes written, 0 on error. */
uint32_t v4b_cmap(uint8_t *out, uint32_t cap, const char *const *names, uint32_t count);
uint32_t v4b_vmap(uint8_t *out, uint32_t cap, const char *variant, const char *size, const char *color);
uint32_t v4b_bcmap(uint8_t *out, uint32_t cap, const uint32_t *entries, uint32_t count);
/* Append one record at `off`; data is inline when reference == 0 (cb bytes), else only the header is written with
 * cb as the logical size. Returns the new offset (8-byte aligned), 0 on error. */
uint32_t v4b_record(uint8_t *out, uint32_t cap, uint32_t off, uint32_t name_id, uint32_t type_id, uint32_t class_id,
                    uint32_t part_id, uint32_t state_id, uint32_t reference, const void *data, uint32_t cb);
/* A string-table block: 16 length-prefixed UTF-16 strings from ASCII (NULL = empty). */
uint32_t v4b_string_block(uint8_t *out, uint32_t cap, const char *const strings[16]);

#endif
