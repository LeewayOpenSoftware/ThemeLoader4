/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the theme style-sheet model and colour resolution
 *              (R-DUI-17, S1-FUZZ-PLAN 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_theme.h>
#include "dui_fuzz_check.h"

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

/* 1. ranges and 2. counts of a loaded model */
static void
check_model(const dui_sheets *s)
{
    uint32_t i, rules = 0, decls = 0, nodes = 0, conds = 0;
    const dui_sheet_stats *st = &s->stats;

    FUZZ_CHECK(st->sheets >= 1 && st->sheets <= DUI_SHEET_MAX_SHEETS && st->scopes <= DUI_SHEET_MAX_SCOPES &&
               st->rules + st->conditions <= DUI_SHEET_MAX_RULES_AND_CONDS &&
               st->declarations <= DUI_SHEET_MAX_DECLARATIONS && st->value_nodes <= DUI_SHEET_MAX_VALUE_NODES);
    FUZZ_CHECK(st->dropped == st->dropped_by_reason[0] + st->dropped_by_reason[1] +
                              st->dropped_by_reason[2] + st->dropped_by_reason[3]);
    for (i = 0; i < st->sheets; i++)
    {
        FUZZ_CHECK((size_t)s->sheets[i].resid.offset + s->sheets[i].resid.length <= s->input_size);
        rules += s->sheets[i].rule_count;
    }
    FUZZ_CHECK(rules == st->rules);
    for (i = 0; i < st->scopes; i++)
    {
        const dui_scope *sc = &s->scopes[i];
        FUZZ_CHECK(sc->parent_scope == DUI_SHEET_NONE || sc->parent_scope < i);
        FUZZ_CHECK((uint64_t)sc->first_condition + sc->condition_count <= st->conditions);
        conds += sc->condition_count;
    }
    for (i = 0; i < st->rules; i++)
    {
        const dui_rule *r = &s->rules[i];
        FUZZ_CHECK(r->sheet < st->sheets && (r->scope == DUI_SHEET_NONE || r->scope < st->scopes));
        FUZZ_CHECK((size_t)r->selector.offset + r->selector.length <= s->input_size);
        FUZZ_CHECK((uint64_t)r->first_declaration + r->declaration_count <= st->declarations);
        FUZZ_CHECK((uint64_t)r->first_condition + r->condition_count <= st->conditions);
        decls += r->declaration_count;
        conds += r->condition_count;
    }
    FUZZ_CHECK(decls == st->declarations && conds == st->conditions);
    for (i = 0; i < st->conditions; i++)
    {
        const dui_condition *c = &s->conditions[i];
        FUZZ_CHECK(c->kind <= DUI_COND_OTHER);
        FUZZ_CHECK((size_t)c->name.offset + c->name.length <= s->input_size);
        FUZZ_CHECK((size_t)c->value.offset + c->value.length <= s->input_size);
    }
    for (i = 0; i < st->declarations; i++)
    {
        const dui_declaration *d = &s->declarations[i];
        FUZZ_CHECK(d->property < DUI_PROP_COUNT && d->node_count >= 1 && d->node_count <= DUI_VALUE_MAX_NODES);
        FUZZ_CHECK(d->first_node == nodes);
        nodes += d->node_count;
    }
    FUZZ_CHECK(nodes == st->value_nodes);
    for (i = 0; i < st->value_nodes; i++)
        FUZZ_CHECK((size_t)s->nodes[i].text.offset + s->nodes[i].text.length <= s->input_size);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_sheets s1, s2, s3;
    dui_status r1, r2, r3;
    uint32_t i, argb;
    uint8_t *copy;
    int pass;

    r1 = dui_sheets_load(data, size, &s1, NULL);
    if (r1 != DUI_OK)
        FUZZ_CHECK(sheets_cleared(&s1)); /* 4. failure leaves the output cleared */
    else
    {
        check_model(&s1);
        for (pass = 0; pass < 2; pass++)
        {
            dui_theme_query q = { pass ? &s1 : NULL, q_active, q_color, q_named };
            for (i = 0; i < s1.stats.declarations; i++)
                (void)dui_resolve_color(&s1, &s1.declarations[i], &q, &argb);
        }
    }

    /* 5. determinism */
    r2 = dui_sheets_load(data, size, &s2, NULL);
    FUZZ_CHECK(r2 == r1 && (r1 != DUI_OK || sheets_eq(&s1, &s2)));
    dui_sheets_free(&s2);

    /* 6. copy independence */
    copy = fuzz_exact_copy(data, size);
    r3 = dui_sheets_load(copy, size, &s3, NULL);
    FUZZ_CHECK(r3 == r1 && (r1 != DUI_OK || sheets_eq(&s1, &s3)));
    dui_sheets_free(&s3);
    free(copy);

    dui_sheets_free(&s1);
    return 0;
}
