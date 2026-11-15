/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the value-expression parser (R-DUI-17)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_value.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static dui_value value;

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_span span;
    uint16_t i;

    if (size > 8192)
        return 0;
    span.offset = 0;
    span.length = (uint32_t)size;
    if (dui_value_parse(data, size, span, &value) != DUI_OK)
        return 0;

    /* invariants of an accepted value */
    if (value.count == 0 || value.count > DUI_VALUE_MAX_NODES || value.nodes[0].parent != DUI_VALUE_NO_NODE)
        __builtin_trap();
    for (i = 0; i < value.count; i++)
    {
        const dui_value_node *n = &value.nodes[i];
        uint16_t c, seen = 0;

        if ((size_t)n->text.offset + n->text.length > size)
            __builtin_trap();
        if (i && (n->parent >= i))
            __builtin_trap();
        for (c = n->first_child; c != DUI_VALUE_NO_NODE; c = value.nodes[c].next)
        {
            if (c <= i || c >= value.count || value.nodes[c].parent != i || ++seen > DUI_VALUE_MAX_ARGS)
                __builtin_trap();
        }
        if (seen != n->child_count)
            __builtin_trap();
    }
    return 0;
}
