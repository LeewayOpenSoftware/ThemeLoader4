/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Value resolution through the abstract theme-query interface
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_theme.h>

/* Node i of a declaration's value (indices are relative to first_node). */
static const dui_value_node *
node_at(const dui_sheets *s, const dui_declaration *d, uint16_t i)
{
    return (i < d->node_count) ? &s->nodes[d->first_node + i] : NULL;
}

static const dui_value_node *
child(const dui_sheets *s, const dui_declaration *d, const dui_value_node *n, unsigned which)
{
    uint16_t c = n->first_child;

    while (c != DUI_VALUE_NO_NODE && which--)
    {
        const dui_value_node *cn = node_at(s, d, c);
        if (!cn)
            return NULL;
        c = cn->next;
    }
    return c == DUI_VALUE_NO_NODE ? NULL : node_at(s, d, c);
}

static int
terminal_color(const dui_sheets *s, const dui_declaration *d, const dui_value_node *n,
               const dui_theme_query *q, uint32_t *argb)
{
    const dui_value_node *a[4];
    unsigned i;

    if (n->kind == DUI_V_NAME)
    {
        return q && q->get_named_color &&
               q->get_named_color(q->context, (const char *)s->input + n->text.offset, n->text.length, argb);
    }
    if (n->kind != DUI_V_CALL)
        return 0;
    for (i = 0; i < 4; i++)
        a[i] = child(s, d, n, i);

    switch (n->function)
    {
        case DUI_FN_ARGB: /* argb(a, r, g, b), components checked 0..255 by the parser */
            *argb = ((uint32_t)a[0]->number << 24) | ((uint32_t)a[1]->number << 16) |
                    ((uint32_t)a[2]->number << 8) | (uint32_t)a[3]->number;
            return 1;
        case DUI_FN_RGB:
            *argb = 0xFF000000u | ((uint32_t)a[0]->number << 16) | ((uint32_t)a[1]->number << 8) |
                    (uint32_t)a[2]->number;
            return 1;
        case DUI_FN_GTC:
            return q && q->get_color &&
                   q->get_color(q->context, (const char *)s->input + a[0]->text.offset, a[0]->text.length,
                                a[1]->number, a[2]->number, a[3]->number, argb);
        default:
            return 0;
    }
}

dui_status
dui_resolve_color(const dui_sheets *s, const dui_declaration *d, const dui_theme_query *q, uint32_t *argb)
{
    const dui_value_node *fallback[DUI_VALUE_MAX_NESTING];
    unsigned nfallback = 0, steps;
    const dui_value_node *n;

    if (!s || !d || !argb || d->node_count == 0)
        return DUI_E_ARG;
    n = node_at(s, d, 0);

    /* Bounded walk: each step either descends one level or takes a pending fallback. */
    for (steps = 0; n && steps < 2 * DUI_VALUE_MAX_NESTING + 2; steps++)
    {
        if (n->kind == DUI_V_CALL && n->function == DUI_FN_THEMEABLE)
        {
            const dui_value_node *themed = child(s, d, n, 0), *plain = child(s, d, n, 1);

            if (q && q->is_active && q->is_active(q->context))
            {
                if (nfallback < DUI_VALUE_MAX_NESTING)
                    fallback[nfallback++] = plain;
                n = themed;
            }
            else
            {
                n = plain;
            }
            continue;
        }
        if (terminal_color(s, d, n, q, argb))
            return DUI_OK;
        n = nfallback ? fallback[--nfallback] : NULL;
    }
    return DUI_E_ARG;
}
