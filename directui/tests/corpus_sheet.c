/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Corpus runner: load the reference theme style sheets; layouts must be refused
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Reads $TL4_CORPUS_DIR/uifile.list like corpus_xml. Entries whose name starts with "theme"
 * are UIFILE id 1 of a style and must load; every other entry is a layout resource, which the
 * theme entry point must refuse (R-DUI-1, R-DUI-2). Skipped (77) without TL4_CORPUS_DIR.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_sheet.h>

#define MAX_PATH_LEN 1024

static int
read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;

    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 || len > 4 * 1024 * 1024)
    {
        fclose(f);
        return 0;
    }
    *size = (size_t)len;
    *data = malloc(*size ? *size : 1);
    if (!*data || fread(*data, 1, *size, f) != *size)
    {
        free(*data);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

int
main(void)
{
    const char *dir = getenv("TL4_CORPUS_DIR");
    char path[MAX_PATH_LEN], line[MAX_PATH_LEN];
    FILE *list;
    unsigned themes = 0, theme_fail = 0, layouts = 0, layout_accepted = 0;
    dui_sheet_stats sum = { 0 }, max = { 0 };

    if (!dir || !dir[0])
    {
        printf("TL4_CORPUS_DIR not set: skipped\n");
        return 77;
    }
    if (snprintf(path, sizeof(path), "%s/uifile.list", dir) >= (int)sizeof(path) || !(list = fopen(path, "r")))
    {
        fprintf(stderr, "cannot open %s/uifile.list\n", dir);
        return 1;
    }
    while (fgets(line, sizeof(line), list))
    {
        uint8_t *data = NULL;
        size_t size = 0, off = 0;
        dui_sheets s;
        dui_status st;
        int is_theme;

        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, line) >= (int)sizeof(path) || !read_file(path, &data, &size))
        {
            fprintf(stderr, "cannot read %s\n", line);
            fclose(list);
            return 1;
        }
        is_theme = strncmp(line, "theme", 5) == 0;
        st = dui_sheets_load(data, size, &s, &off);
        if (is_theme)
        {
            themes++;
            if (st != DUI_OK)
            {
                theme_fail++;
                printf("THEME REJECTED %s: %s at offset %zu\n", line, dui_status_name(st), off);
            }
            else
            {
                const uint32_t *a = &s.stats.sheets, *end = (const uint32_t *)(&s.stats + 1);
                uint32_t *ps = &sum.sheets, *pm = &max.sheets;
                for (; a < end; a++, ps++, pm++)
                {
                    *ps += *a;
                    if (*a > *pm)
                        *pm = *a;
                }
            }
        }
        else
        {
            layouts++;
            if (st == DUI_OK)
            {
                layout_accepted++;
                printf("LAYOUT ACCEPTED %s\n", line);
            }
        }
        dui_sheets_free(&s);
        free(data);
    }
    fclose(list);

    printf("theme sheets: %u, rejected %u; layouts: %u, accepted by the theme entry %u\n",
           themes, theme_fail, layouts, layout_accepted);
    printf("max per theme resource: sheets %u scopes %u rules %u conditions %u declarations %u nodes %u "
           "rules+conditions %u\n", max.sheets, max.scopes, max.rules, max.conditions, max.declarations,
           max.value_nodes, max.rules + max.conditions);
    printf("totals: ignored text runs %u, unknown conditions %u, unknown calls %u, dropped %u (content %u, "
           "identity %u, behaviour %u, unlisted %u)\n", sum.ignored_text, sum.unknown_conditions,
           sum.unknown_calls, sum.dropped,
           sum.dropped_by_reason[0], sum.dropped_by_reason[1], sum.dropped_by_reason[2], sum.dropped_by_reason[3]);
    return (theme_fail || layout_accepted) ? 1 : 0;
}
