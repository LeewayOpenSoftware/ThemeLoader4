/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the value-expression parser (R-DUI-17, S1-FUZZ-PLAN 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include "dui_fuzz_check.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static dui_value v1, v2, v3;

/* 1. ranges and 2. counts of an accepted value */
static void
check_tree(const dui_value *v, size_t size)
{
    uint16_t i;

    FUZZ_CHECK(v->count > 0 && v->count <= DUI_VALUE_MAX_NODES && v->nodes[0].parent == DUI_VALUE_NO_NODE);
    FUZZ_CHECK(v->unknown_calls <= v->count);
    for (i = 0; i < v->count; i++)
    {
        const dui_value_node *n = &v->nodes[i];
        uint16_t c, seen = 0;

        FUZZ_CHECK((size_t)n->text.offset + n->text.length <= size);
        FUZZ_CHECK(n->kind <= DUI_V_CALL && n->function < DUI_FN_COUNT);
        FUZZ_CHECK(!i || n->parent < i);
        for (c = n->first_child; c != DUI_VALUE_NO_NODE; c = v->nodes[c].next)
        {
            FUZZ_CHECK(c > i && c < v->count && v->nodes[c].parent == i && ++seen <= DUI_VALUE_MAX_ARGS);
        }
        FUZZ_CHECK(seen == n->child_count);
    }
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_span span;
    dui_status r1, r2, r3;
    uint8_t *copy;

    if (size > 8192)
        return 0;
    span.offset = 0;
    span.length = (uint32_t)size;

    r1 = dui_value_parse(data, size, span, &v1);
    if (r1 == DUI_OK)
        check_tree(&v1, size);

    /* 5. determinism */
    r2 = dui_value_parse(data, size, span, &v2);
    FUZZ_CHECK(r2 == r1 && (r1 != DUI_OK || values_eq(&v1, &v2)));

    /* 6. copy independence */
    copy = fuzz_exact_copy(data, size);
    r3 = dui_value_parse(copy, size, span, &v3);
    FUZZ_CHECK(r3 == r1 && (r1 != DUI_OK || values_eq(&v1, &v3)));
    free(copy);
    return 0;
}
