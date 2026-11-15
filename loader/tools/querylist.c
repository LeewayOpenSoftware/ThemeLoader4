/*
 * PROJECT:     Theme Loader 4 - loader tools
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Query list for the Windows reference probe (tools/vsprobe): every VARIANT property of a style's model
 *              as "class,part,state,property,type" CSV, so the probe asks exactly what the file defines
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Usage: tl4_loader_querylist <style.msstyles> [strings.mui] > queries.csv
 * The output holds class names and numeric ids only; it carries no byte of the style's values.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/model.h>

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (!buf) { fclose(f); return 0; }
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

/* UTF-16 class name as UTF-8 (the corpus names are ASCII; anything else is encoded, never dropped) */
static void put_utf8(const uint8_t *utf16, uint32_t chars)
{
    uint32_t i;
    for (i = 0; i < chars; i++)
    {
        uint32_t c = (uint32_t)utf16[2 * i] | ((uint32_t)utf16[2 * i + 1] << 8);
        if (c < 0x80) putchar((int)c);
        else if (c < 0x800) { putchar(0xC0 | (int)(c >> 6)); putchar(0x80 | (int)(c & 0x3F)); }
        else { putchar(0xE0 | (int)(c >> 12)); putchar(0x80 | (int)((c >> 6) & 0x3F)); putchar(0x80 | (int)(c & 0x3F)); }
    }
}

int main(int argc, char **argv)
{
    size_t size, msize = 0, need;
    uint8_t *data, *mdata = 0;
    tl4_pe pe, mui, *strings = 0;
    tl4_v4_stats s;
    tl4_model model;
    void *buf;
    uint32_t i, last_class = 0xFFFFFFFFu;
    const uint8_t *name = 0; uint32_t chars = 0;

    if (argc < 2 || argc > 3) { fprintf(stderr, "usage: tl4_loader_querylist <style.msstyles> [strings.mui]\n"); return 2; }
    if (!(data = read_file(argv[1], &size))) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    if (argc == 3)
    {
        if (!(mdata = read_file(argv[2], &msize)) || tl4_pe_open(mdata, msize, &mui) != TL4_OK) { fprintf(stderr, "strings module rejected\n"); return 3; }
        strings = &mui;
    }
    if (tl4_pe_open(data, size, &pe) != TL4_OK || tl4_v4_validate(&pe, strings, &s) != TL4_OK) { fprintf(stderr, "style rejected\n"); return 3; }
    need = tl4_model_size(&s);
    buf = malloc(need);
    if (!buf || tl4_model_build(&pe, strings, buf, need, &model) != TL4_OK) { fprintf(stderr, "model not built\n"); return 3; }

    printf("# class,part,state,property,type   (from %s: %u properties, %u classes)\n", argv[1], model.nprops, model.nclasses);
    for (i = 0; i < model.nprops; i++)
    {
        const tl4_prop *p = &model.props[i];
        if (p->area != TL4_AREA_VARIANT) continue;                         /* the probe's GetTheme* see the VARIANT only */
        if (p->class_ordinal != last_class)
        {
            if (tl4_model_class_name(&model, p->class_ordinal, &name, &chars) != TL4_OK) { name = 0; chars = 0; }
            last_class = p->class_ordinal;
        }
        if (!name) continue;
        put_utf8(name, chars);
        printf(",%u,%u,%u,%u\n", p->part, p->state, p->name_id, p->type);
    }
    free(buf); free(mdata); free(data);
    return 0;
}
