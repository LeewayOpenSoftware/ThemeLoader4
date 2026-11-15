/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the consumer layout entry point
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_layout.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_layout l;
    uint32_t i, walked = 0;

    if (dui_layout_load(data, size, &l, NULL) != DUI_OK)
        return 0;
    for (i = 0; i < l.element_count; i++)
    {
        const dui_element *e = &l.elements[i];
        uint32_t c;

        if ((size_t)e->tag.offset + e->tag.length > l.input_size ||
            (uint64_t)e->first_attribute + e->attribute_count > l.attribute_count ||
            (i && e->parent >= i) || (!i && e->parent != DUI_LAYOUT_NONE))
        {
            __builtin_trap();
        }
        for (c = e->first_child; c != DUI_LAYOUT_NONE; c = l.elements[c].next)
        {
            if (c <= i || c >= l.element_count || l.elements[c].parent != i)
                __builtin_trap();
            walked++;
        }
    }
    if (l.element_count && walked != l.element_count - 1)
        __builtin_trap();
    dui_layout_free(&l);
    return 0;
}
