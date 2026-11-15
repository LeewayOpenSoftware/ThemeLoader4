/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the theme style-sheet model and colour resolution
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_theme.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int q_active(void *c) { return c != NULL; }
static int q_color(void *c, const char *n, size_t l, int p, int s, int pr, uint32_t *argb)
{
    (void)c; (void)n; (void)p; (void)s; (void)pr;
    *argb = (uint32_t)l;
    return (l & 1) != 0;
}
static int q_named(void *c, const char *n, size_t l, uint32_t *argb)
{
    (void)c; (void)n;
    *argb = (uint32_t)l;
    return 1;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_sheets s;
    uint32_t i, argb;
    int pass;

    if (dui_sheets_load(data, size, &s, NULL) != DUI_OK)
        return 0;

    for (i = 0; i < s.stats.rules; i++)
    {
        const dui_rule *r = &s.rules[i];
        if (r->sheet >= s.stats.sheets || (r->scope != DUI_SHEET_NONE && r->scope >= s.stats.scopes) ||
            (uint64_t)r->first_declaration + r->declaration_count > s.stats.declarations ||
            (uint64_t)r->first_condition + r->condition_count > s.stats.conditions)
        {
            __builtin_trap();
        }
    }
    for (pass = 0; pass < 2; pass++)
    {
        dui_theme_query q = { pass ? &s : NULL, q_active, q_color, q_named };
        for (i = 0; i < s.stats.declarations; i++)
        {
            const dui_declaration *d = &s.declarations[i];
            if ((uint64_t)d->first_node + d->node_count > s.stats.value_nodes || d->property >= DUI_PROP_COUNT)
                __builtin_trap();
            (void)dui_resolve_color(&s, d, &q, &argb);
        }
    }
    dui_sheets_free(&s);
    return 0;
}
