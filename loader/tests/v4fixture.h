/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     A complete generated version-4 style image, shared by the unit tests, the seed tool and the harnesses
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#ifndef TL4_LOADER_V4FIXTURE_H
#define TL4_LOADER_V4FIXTURE_H

#include <stddef.h>
#include <stdint.h>
#include "pebuild.h"

/* The parts of the generated style, so a test can corrupt one of them before the image is assembled. */
typedef struct v4f_parts {
    uint8_t version[2];
    uint8_t cmap[1024];      uint32_t cmap_size;
    uint8_t vmap[128];       uint32_t vmap_size;
    uint8_t bcmap[256];      uint32_t bcmap_size;
    uint8_t rmap[256];       uint32_t rmap_size;
    uint8_t variant[2048];   uint32_t variant_size;
    uint8_t strings[512];    uint32_t strings_size;   /* string block #32 (ids 496..511) */
    uint8_t image[512];      uint32_t image_size;
    uint8_t stream[512];     uint32_t stream_size;
    int omit_bcmap, omit_strings, omit_image_509;
    uint32_t extra_language_of;  /* 0 = none; else the 1-based index of the resource that gets a second language entry
                                  * (id 1031) for the R-PE-8 tests: 1 = PACKTHEM_VERSION ... 10 = the string block */
} v4f_parts;

/* Fill the parts with the reference shape: 6 names (documentation, sizevariant.NormalSize, colorvariant.NormalColor,
 * globals, Button, Explorer::Button), BCMAP for the 3 classes, RMAP with the display name, a VARIANT with inline
 * values of every type plus FONT/FILENAME/DISKSTREAM references, a string block holding the font at 501, two IMAGE
 * resources (508, 509) and one STREAM (1718). */
void v4f_default(v4f_parts *p);
/* Assemble the PE image from the parts. Returns the bytes written, 0 on error. */
size_t v4f_build(const v4f_parts *p, uint8_t *out, size_t cap);

#endif
