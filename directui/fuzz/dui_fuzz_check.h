/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Shared checks for the DirectUI fuzz harnesses (S1-FUZZ-PLAN section 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Field-by-field comparison of results (structures have padding, so no memcmp on them), and
 * the "copy at the end of an exact allocation" helper for the copy-independence check.
 */

#ifndef TL4_DIRECTUI_FUZZ_CHECK_H
#define TL4_DIRECTUI_FUZZ_CHECK_H

#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_layout.h>
#include <tl4/directui/dui_sheet.h>
#include <tl4/directui/dui_value.h>

#define FUZZ_CHECK(c) do { if (!(c)) __builtin_trap(); } while (0)

/* A copy of the input in an allocation of exactly its size: a one-byte over-read faults. */
static inline uint8_t *
fuzz_exact_copy(const uint8_t *data, size_t size)
{
    uint8_t *copy = malloc(size ? size : 1);

    FUZZ_CHECK(copy != NULL);
    if (size)
        memcpy(copy, data, size);
    return copy;
}

static inline int
span_eq(dui_span a, dui_span b)
{
    return a.offset == b.offset && a.length == b.length;
}

static inline int
value_node_eq(const dui_value_node *a, const dui_value_node *b)
{
    return a->kind == b->kind && a->function == b->function && a->parent == b->parent &&
           a->first_child == b->first_child && a->next == b->next && a->child_count == b->child_count &&
           a->number == b->number && span_eq(a->text, b->text);
}

static inline int
values_eq(const dui_value *a, const dui_value *b)
{
    uint16_t i;

    if (a->count != b->count || a->unknown_calls != b->unknown_calls)
        return 0;
    for (i = 0; i < a->count; i++)
    {
        if (!value_node_eq(&a->nodes[i], &b->nodes[i]))
            return 0;
    }
    return 1;
}

static inline int
sheet_stats_eq(const dui_sheet_stats *a, const dui_sheet_stats *b)
{
    unsigned i;

    if (a->sheets != b->sheets || a->scopes != b->scopes || a->rules != b->rules ||
        a->conditions != b->conditions || a->declarations != b->declarations ||
        a->value_nodes != b->value_nodes || a->unknown_conditions != b->unknown_conditions ||
        a->unknown_calls != b->unknown_calls || a->dropped != b->dropped || a->ignored_text != b->ignored_text)
    {
        return 0;
    }
    for (i = 0; i < 4; i++)
    {
        if (a->dropped_by_reason[i] != b->dropped_by_reason[i])
            return 0;
    }
    return 1;
}

/* Two loaded sheet models are equal field by field, including their private input copies. */
static inline int
sheets_eq(const dui_sheets *a, const dui_sheets *b)
{
    uint32_t i;

    if (!sheet_stats_eq(&a->stats, &b->stats) || a->input_size != b->input_size ||
        memcmp(a->input, b->input, a->input_size) != 0)
    {
        return 0;
    }
    for (i = 0; i < a->stats.sheets; i++)
    {
        if (!span_eq(a->sheets[i].resid, b->sheets[i].resid) || a->sheets[i].first_rule != b->sheets[i].first_rule ||
            a->sheets[i].rule_count != b->sheets[i].rule_count)
            return 0;
    }
    for (i = 0; i < a->stats.scopes; i++)
    {
        if (a->scopes[i].parent_scope != b->scopes[i].parent_scope ||
            a->scopes[i].first_condition != b->scopes[i].first_condition ||
            a->scopes[i].condition_count != b->scopes[i].condition_count)
            return 0;
    }
    for (i = 0; i < a->stats.rules; i++)
    {
        const dui_rule *x = &a->rules[i], *y = &b->rules[i];
        if (!span_eq(x->selector, y->selector) || x->sheet != y->sheet || x->scope != y->scope ||
            x->first_condition != y->first_condition || x->condition_count != y->condition_count ||
            x->first_declaration != y->first_declaration || x->declaration_count != y->declaration_count)
            return 0;
    }
    for (i = 0; i < a->stats.conditions; i++)
    {
        if (a->conditions[i].kind != b->conditions[i].kind || !span_eq(a->conditions[i].name, b->conditions[i].name) ||
            !span_eq(a->conditions[i].value, b->conditions[i].value))
            return 0;
    }
    for (i = 0; i < a->stats.declarations; i++)
    {
        if (a->declarations[i].property != b->declarations[i].property ||
            a->declarations[i].node_count != b->declarations[i].node_count ||
            a->declarations[i].first_node != b->declarations[i].first_node)
            return 0;
    }
    for (i = 0; i < a->stats.value_nodes; i++)
    {
        if (!value_node_eq(&a->nodes[i], &b->nodes[i]))
            return 0;
    }
    return 1;
}

static inline int
layouts_eq(const dui_layout *a, const dui_layout *b)
{
    uint32_t i;

    if (a->element_count != b->element_count || a->attribute_count != b->attribute_count ||
        a->text_runs != b->text_runs || a->macros != b->macros || a->binds != b->binds ||
        a->input_size != b->input_size || memcmp(a->input, b->input, a->input_size) != 0)
    {
        return 0;
    }
    for (i = 0; i < a->element_count; i++)
    {
        const dui_element *x = &a->elements[i], *y = &b->elements[i];
        if (!span_eq(x->tag, y->tag) || x->parent != y->parent || x->first_child != y->first_child ||
            x->next != y->next || x->first_attribute != y->first_attribute || x->attribute_count != y->attribute_count)
            return 0;
    }
    for (i = 0; i < a->attribute_count; i++)
    {
        if (!span_eq(a->attributes[i].name, b->attributes[i].name) ||
            !span_eq(a->attributes[i].value, b->attributes[i].value))
            return 0;
    }
    return 1;
}

/* A failed load leaves its output cleared (R-GEN-5). */
static inline int
sheets_cleared(const dui_sheets *s)
{
    return !s->input && !s->sheets && !s->scopes && !s->rules && !s->conditions && !s->declarations && !s->nodes;
}

static inline int
layout_cleared(const dui_layout *l)
{
    return !l->input && !l->elements && !l->attributes && !l->element_count && !l->attribute_count;
}

#endif /* TL4_DIRECTUI_FUZZ_CHECK_H */
