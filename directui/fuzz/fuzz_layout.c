/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the consumer layout entry point (S1-FUZZ-PLAN 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_xml.h>
#include "dui_fuzz_check.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* 1. ranges and 2. counts of a loaded layout */
static void
check_layout(const dui_layout *l)
{
    uint32_t i, walked = 0, attrs = 0;

    FUZZ_CHECK(l->element_count >= 1 && l->element_count <= DUI_XML_MAX_ELEMENTS &&
               l->attribute_count <= DUI_XML_MAX_ATTRS_TOTAL);
    FUZZ_CHECK(l->macros + l->binds <= l->element_count);
    for (i = 0; i < l->element_count; i++)
    {
        const dui_element *e = &l->elements[i];
        uint32_t c;

        FUZZ_CHECK((size_t)e->tag.offset + e->tag.length <= l->input_size);
        FUZZ_CHECK((uint64_t)e->first_attribute + e->attribute_count <= l->attribute_count);
        FUZZ_CHECK(e->first_attribute == attrs && e->attribute_count <= DUI_XML_MAX_ATTRS_PER_ELEM);
        FUZZ_CHECK(i ? e->parent < i : e->parent == DUI_LAYOUT_NONE);
        attrs += e->attribute_count;
        for (c = e->first_child; c != DUI_LAYOUT_NONE; c = l->elements[c].next)
        {
            FUZZ_CHECK(c > i && c < l->element_count && l->elements[c].parent == i);
            walked++;
        }
    }
    FUZZ_CHECK(attrs == l->attribute_count && walked == l->element_count - 1);
    for (i = 0; i < l->attribute_count; i++)
    {
        FUZZ_CHECK((size_t)l->attributes[i].name.offset + l->attributes[i].name.length <= l->input_size);
        FUZZ_CHECK((size_t)l->attributes[i].value.offset + l->attributes[i].value.length <= l->input_size);
    }
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_layout l1, l2, l3;
    dui_status r1, r2, r3;
    uint8_t *copy;

    r1 = dui_layout_load(data, size, &l1, NULL);
    if (r1 != DUI_OK)
        FUZZ_CHECK(layout_cleared(&l1)); /* 4. failure leaves the output cleared */
    else
        check_layout(&l1);

    /* 3. agreement: the layout entry accepts exactly what the tokenizer accepts */
    FUZZ_CHECK((dui_xml_parse(data, size, NULL, NULL, NULL, NULL) == DUI_OK) == (r1 == DUI_OK) ||
               r1 == DUI_E_NO_MEMORY);

    /* 5. determinism */
    r2 = dui_layout_load(data, size, &l2, NULL);
    FUZZ_CHECK(r2 == r1 && (r1 != DUI_OK || layouts_eq(&l1, &l2)));
    dui_layout_free(&l2);

    /* 6. copy independence */
    copy = fuzz_exact_copy(data, size);
    r3 = dui_layout_load(copy, size, &l3, NULL);
    FUZZ_CHECK(r3 == r1 && (r1 != DUI_OK || layouts_eq(&l1, &l3)));
    dui_layout_free(&l3);
    free(copy);

    dui_layout_free(&l1);
    return 0;
}
