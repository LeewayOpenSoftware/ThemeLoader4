/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Writes generated seed inputs for the fuzzers (R-PROC-1): PE images for fuzz_pe and fuzz_v4_style,
 *              raw blobs for fuzz_v4_blob. Nothing here is copied from a real style.
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * usage: mkseeds <seed root>   writes the seeds into <root>/pe, <root>/v4_style and <root>/v4_blob
 */
#include <stdio.h>
#include <string.h>
#include "pebuild.h"
#include "v4fixture.h"
#include "v4build.h"
#include "pngbuild.h"

static uint8_t image[64 * 1024];
static uint8_t prefixed[64 * 1024 + 2];
static uint8_t pair[2 * 64 * 1024];

static int write_file(const char *dir, const char *sub, const char *name, const uint8_t *data, size_t size)
{
    char path[1024];
    FILE *f;
    if (strlen(dir) + strlen(sub) + strlen(name) + 3 > sizeof(path)) return 1;
    snprintf(path, sizeof(path), "%s/%s/%s", dir, sub, name);
    f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    if (fwrite(data, 1, size, f) != size) { fclose(f); return 1; }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    static const uint8_t version4[2] = {4, 0};
    static const uint8_t version3[2] = {3, 0};
    static const uint8_t blob[64] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 1};
    static const uint8_t cmap[] = {'g', 0, 'l', 0, 'o', 0, 'b', 0, 'a', 0, 'l', 0, 's', 0, 0, 0, 'W', 0, 'i', 0, 'n', 0, 'd', 0, 'o', 0, 'w', 0, 0, 0, 0, 0};
    pb_res style[] = {
        {"PACKTHEM_VERSION", 0, NULL, 1, 0, version4, 2},
        {"CMAP", 0, "CMAP", 0, 0, cmap, sizeof(cmap)},
        {"BCMAP", 0, "BCMAP", 0, 0, blob, 8},
        {"VMAP", 0, "VMAP", 0, 0, blob, 16},
        {"RMAP", 0, "RMAP", 0, 0, blob, 40},
        {"VARIANT", 0, "NORMAL", 0, 0, blob, 64},
        {"IMAGE", 0, NULL, 508, 0, blob, 32},
        {"IMAGE", 0, NULL, 509, 0, blob, 24},
        {"STREAM", 0, NULL, 1718, 0, blob, 64},
        {NULL, 6, NULL, 32, 1033, blob, 48},
        {NULL, 16, NULL, 1, 1033, blob, 16},
    };
    pb_res v3[] = {
        {"PACKTHEM_VERSION", 0, NULL, 1, 0, version3, 2},
        {"COLORNAMES", 0, NULL, 1, 0, blob, 26},
        {"SIZENAMES", 0, NULL, 1, 0, blob, 26},
        {"TEXTFILE", 0, "NORMALNORMAL_INI", 0, 0, blob, 64},
    };
    pb_res one[] = {{NULL, 10, NULL, 1, 0, blob, 1}};
    pb_res langs[] = {   /* R-PE-8 as amended: several language entries per pair, PACKTHEM_VERSION with one */
        {"PACKTHEM_VERSION", 0, NULL, 1, 0, version3, 2},
        {"IMAGE", 0, NULL, 508, 0, blob, 32},
        {"IMAGE", 0, NULL, 508, 1031, blob, 24},
        {"COLORNAMES", 0, NULL, 1, 1033, blob, 26},
        {"COLORNAMES", 0, NULL, 1, 1036, blob, 26},
        {"COLORNAMES", 0, NULL, 1, 3082, blob, 26},
        {NULL, 6, NULL, 32, 1031, blob, 48},
        {NULL, 6, NULL, 32, 1033, blob, 48},
        {NULL, 6, NULL, 32, 1036, blob, 40},
        {NULL, 6, NULL, 33, 1033, blob, 48},
    };
    pb_options o32plus = {1, 0, 0, 0}, o32 = {0, 0, 0, 0}, osecs = {1, 0x3000, 2, 0};
    v4f_parts parts;
    uint8_t blobbuf[4096];
    size_t n;
    const char *dir;
    int rc = 0;

    if (argc != 2) { fprintf(stderr, "usage: mkseeds <seed root>\n"); return 2; }
    dir = argv[1];

    /* fuzz_pe: resource trees of several shapes */
    n = pb_build(style, sizeof(style) / sizeof(style[0]), &o32plus, image, sizeof(image), NULL); rc |= !n || write_file(dir, "pe", "style_pe32plus.bin", image, n);
    n = pb_build(style, sizeof(style) / sizeof(style[0]), &o32, image, sizeof(image), NULL);     rc |= !n || write_file(dir, "pe", "style_pe32.bin", image, n);
    n = pb_build(style, sizeof(style) / sizeof(style[0]), &osecs, image, sizeof(image), NULL);   rc |= !n || write_file(dir, "pe", "style_sections.bin", image, n);
    n = pb_build(v3, sizeof(v3) / sizeof(v3[0]), &o32, image, sizeof(image), NULL);              rc |= !n || write_file(dir, "pe", "v3_like.bin", image, n);
    n = pb_build(one, 1, &o32plus, image, sizeof(image), NULL);                                  rc |= !n || write_file(dir, "pe", "one_resource.bin", image, n);
    n = pb_build(langs, sizeof(langs) / sizeof(langs[0]), &o32, image, sizeof(image), NULL);     rc |= !n || write_file(dir, "pe", "v3_languages.bin", image, n);
    v4f_default(&parts); parts.extra_language_of = 7;                                            /* IMAGE 508 in two languages */
    n = v4f_build(&parts, image, sizeof(image));                                                 rc |= !n || write_file(dir, "pe", "style_languages.bin", image, n);

    /* fuzz_v4_style: a complete valid style, and two near misses the validator rejects */
    v4f_default(&parts);
    n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_style", "valid.bin", image, n);
    v4f_default(&parts); parts.omit_bcmap = 1;
    n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_style", "no_bcmap.bin", image, n);
    v4f_default(&parts); parts.variant_size = 0;
    n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_style", "empty_variant.bin", image, n);

    /* fuzz_v4_model: the same styles (the last 16 bytes drive the queries), plus one with duplicate records and names */
    {
        static const char *const dup[] = {"documentation", "sizevariant.NormalSize", "colorvariant.NormalColor", "globals", "Button", "Button"};
        static const uint8_t other[4] = {0x78, 0x56, 0, 0};
        v4f_default(&parts);
        n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_model", "valid.bin", image, n);
        v4f_default(&parts); parts.omit_bcmap = 1;
        n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_model", "no_bcmap.bin", image, n);
        v4f_default(&parts); parts.variant_size = 0;
        n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_model", "empty_variant.bin", image, n);
        v4f_default(&parts);
        parts.cmap_size = v4b_cmap(parts.cmap, sizeof(parts.cmap), dup, 6);
        parts.variant_size = v4b_record(parts.variant, sizeof(parts.variant), parts.variant_size, 3804, 204, 3, 0, 0, 0, other, 4);
        n = v4f_build(&parts, image, sizeof(image)); rc |= !n || write_file(dir, "v4_model", "duplicates.bin", image, n);
    }

    /* fuzz_v4_two_pass: two styles of the same length, A then B (the build runs on A, the hook swaps B in) */
    {
        static const uint8_t other[4] = {0x78, 0x56, 0, 0};
        size_t na, nb;
        v4f_default(&parts);
        na = v4f_build(&parts, image, sizeof(image)); memcpy(pair, image, na); memcpy(pair + na, image, na);
        rc |= !na || write_file(dir, "v4_two_pass", "valid_valid.bin", pair, 2 * na);
        /* B: the same style with one value changed in place, so the two images keep their length */
        v4f_default(&parts); memcpy(parts.variant + 32, other, 4);
        nb = v4f_build(&parts, image, sizeof(image));
        rc |= !nb || nb != na;
        if (nb == na) { memcpy(pair + na, image, nb); rc |= write_file(dir, "v4_two_pass", "valid_dups.bin", pair, 2 * na); }
        /* B: the same style with a second language entry on an IMAGE (R-PE-8): B validates on its own only as a reject */
        v4f_default(&parts); parts.extra_language_of = 7;
        nb = v4f_build(&parts, image, sizeof(image));
        rc |= !nb || nb != na;
        if (nb == na) { memcpy(pair + na, image, nb); rc |= write_file(dir, "v4_two_pass", "valid_lang.bin", pair, 2 * na); }
    }

    /* fuzz_png: the corpus' three chunk orders and the bare minimum, all generated */
    n = pngb_image(image, sizeof(image), 16, 8, 6, 200, 1, 0, 1);  rc |= !n || write_file(dir, "png", "rgba_text.bin", image, n);
    n = pngb_image(image, sizeof(image), 24, 12, 2, 300, 3, 1, 0); rc |= !n || write_file(dir, "png", "rgb_srgb.bin", image, n);
    n = pngb_image(image, sizeof(image), 8, 8, 6, 100, 2, 1, 1);   rc |= !n || write_file(dir, "png", "rgba_srgb_text.bin", image, n);
    {
        uint32_t off = pngb_begin(image, sizeof(image));
        off = pngb_ihdr(image, sizeof(image), off, 1, 1, 8, 2, 0);
        off = pngb_chunk(image, sizeof(image), off, "IDAT", 0, 4);
        off = pngb_chunk(image, sizeof(image), off, "IEND", 0, 0);
        rc |= !off || write_file(dir, "png", "minimal.bin", image, off);
    }

    /* fuzz_v4_blob: selector byte, CMAP-count byte, then the blob */
    v4f_default(&parts);
    blobbuf[0] = 0; blobbuf[1] = 5; memcpy(blobbuf + 2, parts.cmap, parts.cmap_size);          rc |= write_file(dir, "v4_blob", "cmap.bin", blobbuf, 2 + parts.cmap_size);
    blobbuf[0] = 1; blobbuf[1] = 0; memcpy(blobbuf + 2, parts.vmap, parts.vmap_size);          rc |= write_file(dir, "v4_blob", "vmap.bin", blobbuf, 2 + parts.vmap_size);
    blobbuf[0] = 2; blobbuf[1] = 5; memcpy(blobbuf + 2, parts.bcmap, parts.bcmap_size);        rc |= write_file(dir, "v4_blob", "bcmap.bin", blobbuf, 2 + parts.bcmap_size);
    blobbuf[0] = 3; blobbuf[1] = 5; memcpy(blobbuf + 2, parts.variant, parts.variant_size);    rc |= write_file(dir, "v4_blob", "variant.bin", blobbuf, 2 + parts.variant_size);
    blobbuf[0] = 3; blobbuf[1] = 5; memcpy(blobbuf + 2, parts.rmap, parts.rmap_size);          rc |= write_file(dir, "v4_blob", "rmap.bin", blobbuf, 2 + parts.rmap_size);
    /* selector 4: the whole style as a PE, the second byte picks string ids (5 -> 501, the font) */
    n = v4f_build(&parts, image, sizeof(image));
    prefixed[0] = 4; prefixed[1] = 5; memcpy(prefixed + 2, image, n);                          rc |= !n || write_file(dir, "v4_blob", "strings.bin", prefixed, 2 + n);

    if (rc) { fprintf(stderr, "mkseeds: failed\n"); return 1; }
    printf("mkseeds: 27 seeds written under %s\n", dir);
    return 0;
}
