/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Writes generated seed inputs for the fuzzers (R-PROC-1); the seeds come from tests/pebuild.c
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include "pebuild.h"

static uint8_t image[64 * 1024];

static int write_file(const char *dir, const char *name, const uint8_t *data, size_t size)
{
    char path[1024];
    FILE *f;
    if (strlen(dir) + strlen(name) + 2 > sizeof(path)) return 1;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
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
    pb_options o32plus = {1, 0, 0, 0}, o32 = {0, 0, 0, 0}, osecs = {1, 0x3000, 2, 0};
    size_t n;
    const char *dir;
    int rc = 0;

    if (argc != 2) { fprintf(stderr, "usage: mkseeds <directory>\n"); return 2; }
    dir = argv[1];

    n = pb_build(style, sizeof(style) / sizeof(style[0]), &o32plus, image, sizeof(image), NULL); rc |= !n || write_file(dir, "style_pe32plus.bin", image, n);
    n = pb_build(style, sizeof(style) / sizeof(style[0]), &o32, image, sizeof(image), NULL);     rc |= !n || write_file(dir, "style_pe32.bin", image, n);
    n = pb_build(style, sizeof(style) / sizeof(style[0]), &osecs, image, sizeof(image), NULL);   rc |= !n || write_file(dir, "style_sections.bin", image, n);
    n = pb_build(v3, sizeof(v3) / sizeof(v3[0]), &o32, image, sizeof(image), NULL);              rc |= !n || write_file(dir, "v3_like.bin", image, n);
    n = pb_build(one, 1, &o32plus, image, sizeof(image), NULL);                                  rc |= !n || write_file(dir, "one_resource.bin", image, n);
    if (rc) { fprintf(stderr, "mkseeds: failed\n"); return 1; }
    printf("mkseeds: 5 seeds written to %s\n", dir);
    return 0;
}
