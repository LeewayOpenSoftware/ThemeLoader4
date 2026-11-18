/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     A complete generated version-4 style image
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include "v4fixture.h"
#include "v4build.h"
#include <string.h>

void v4f_default(v4f_parts *p)
{
    static const char *const names[] = {"documentation", "sizevariant.NormalSize", "colorvariant.NormalColor",
                                        "globals", "Button", "Explorer::Button"};
    static const uint32_t bases[] = {0xFFFFFFFFu, 0u, 1u};            /* globals: none; Button -> globals; Explorer::Button -> Button */
    static const uint8_t color[4] = {0x34, 0x12, 0x00, 0x00};
    static const uint8_t one[4] = {1, 0, 0, 0};
    static const uint8_t margins[16] = {1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0};
    static const uint8_t pos[8] = {5, 0, 0, 0, 6, 0, 0, 0};
    static const uint8_t intlist[12] = {2, 0, 0, 0, 1, 0, 0, 0, 10, 0, 0, 0}; /* two values: 1-state transition matrix */
    static const uint8_t str[10] = {'A', 0, 'e', 0, 'r', 0, 'o', 0, 0, 0};
    static const uint8_t png[32] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1, 8, 6, 0, 0, 0, 0, 0, 0};
    const char *strings[16] = {0};
    uint32_t off;

    memset(p, 0, sizeof(*p));
    p->version[0] = 4;
    p->cmap_size = v4b_cmap(p->cmap, sizeof(p->cmap), names, 6);
    p->vmap_size = v4b_vmap(p->vmap, sizeof(p->vmap), "Normal", "NormalSize", "NormalColor");
    p->bcmap_size = v4b_bcmap(p->bcmap, sizeof(p->bcmap), bases, 3);
    /* RMAP: display name (600, STRING) at class 0 */
    p->rmap_size = v4b_record(p->rmap, sizeof(p->rmap), 0, 600, 201, 0, 0, 0, 0, str, sizeof(str));
    /* VARIANT: globals (class 3) then Button (class 4) parts/states, then Explorer::Button (class 5) */
    off = 0;
    off = v4b_record(p->variant, sizeof(p->variant), off, 3804, 204, 3, 0, 0, 0, color, 4);        /* COLOR */
    off = v4b_record(p->variant, sizeof(p->variant), off, 1001, 203, 3, 0, 0, 0, one, 4);          /* BOOL */
    off = v4b_record(p->variant, sizeof(p->variant), off, 2401, 202, 3, 0, 0, 0, one, 4);          /* INT */
    off = v4b_record(p->variant, sizeof(p->variant), off, 803, 210, 3, 0, 0, 501, 0, 92);          /* FONT -> string 501 */
    off = v4b_record(p->variant, sizeof(p->variant), off, 4001, 200, 4, 1, 0, 0, one, 4);          /* ENUM */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3601, 205, 4, 1, 0, 0, margins, 16);     /* MARGINS */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3001, 206, 4, 1, 0, 508, 0, 0);          /* FILENAME -> IMAGE 508 */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3002, 206, 4, 1, 1, 509, 0, 0);          /* FILENAME -> IMAGE 509 */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3401, 208, 4, 1, 1, 0, pos, 8);          /* POSITION */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3402, 209, 4, 1, 2, 0, margins, 16);     /* RECT */
    off = v4b_record(p->variant, sizeof(p->variant), off, 2201, 207, 4, 1, 2, 0, one, 4);          /* SIZE */
    off = v4b_record(p->variant, sizeof(p->variant), off, 6000, 211, 4, 2, 0, 0, intlist, sizeof(intlist)); /* INTLIST */
    off = v4b_record(p->variant, sizeof(p->variant), off, 8000, 213, 5, 0, 0, 1718, 0, 0);         /* DISKSTREAM -> STREAM 1718 */
    off = v4b_record(p->variant, sizeof(p->variant), off, 3805, 204, 5, 0, 0, 0, color, 4);        /* COLOR */
    p->variant_size = off;
    strings[501 - 496] = "Segoe UI, 9, Quality:ClearType";
    p->strings_size = v4b_string_block(p->strings, sizeof(p->strings), strings);
    memcpy(p->image, png, 32);
    memcpy(p->stream, png, 32);
}

size_t v4f_build(const v4f_parts *p, uint8_t *out, size_t cap)
{
    pb_res res[13];
    uint32_t n = 0;
    pb_options o = {1, 0, 0, 0};

    res[n].type_name = "PACKTHEM_VERSION"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 1; res[n].language = 0; res[n].data = p->version; res[n].size = 2; n++;
    res[n].type_name = "CMAP"; res[n].type_id = 0; res[n].name_name = "CMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = p->cmap; res[n].size = p->cmap_size; n++;
    res[n].type_name = "VMAP"; res[n].type_id = 0; res[n].name_name = "VMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = p->vmap; res[n].size = p->vmap_size; n++;
    if (!p->omit_bcmap) { res[n].type_name = "BCMAP"; res[n].type_id = 0; res[n].name_name = "BCMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = p->bcmap; res[n].size = p->bcmap_size; n++; }
    res[n].type_name = "RMAP"; res[n].type_id = 0; res[n].name_name = "RMAP"; res[n].name_id = 0; res[n].language = 0; res[n].data = p->rmap; res[n].size = p->rmap_size; n++;
    res[n].type_name = "VARIANT"; res[n].type_id = 0; res[n].name_name = "NORMAL"; res[n].name_id = 0; res[n].language = 0; res[n].data = p->variant; res[n].size = p->variant_size; n++;
    res[n].type_name = "IMAGE"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 508; res[n].language = 0; res[n].data = p->image; res[n].size = 32; n++;
    if (!p->omit_image_509) { res[n].type_name = "IMAGE"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 509; res[n].language = 0; res[n].data = p->image; res[n].size = 32; n++; }
    res[n].type_name = "STREAM"; res[n].type_id = 0; res[n].name_name = 0; res[n].name_id = 1718; res[n].language = 0; res[n].data = p->stream; res[n].size = 32; n++;
    if (!p->omit_strings) { res[n].type_name = 0; res[n].type_id = 6; res[n].name_name = 0; res[n].name_id = 32; res[n].language = 1033; res[n].data = p->strings; res[n].size = p->strings_size; n++; }
    if (p->extra_language_of >= 1 && p->extra_language_of <= n)
    {
        res[n] = res[p->extra_language_of - 1];
        res[n].language = 1031;
        n++;
    }
    return pb_build(res, n, &o, out, cap, 0);
}
