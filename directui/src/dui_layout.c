/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Layout resources owned by the consuming binary (R-DUI-4)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Two passes over the tokenizer, as for style sheets (R-GEN-7): count without allocating,
 * allocate exactly that, fill from a private copy, and require the same counts. The input may
 * change between the passes (R-GEN-11): every write of the second pass is checked against the
 * allocated sizes before it happens.
 */

#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_layout.h>
#include <tl4/directui/dui_xml.h>

/* Test hook (R-GEN-11 test): called between the passes with the caller's buffer. */
void (*dui_layout_test_between_passes)(const uint8_t *data, size_t size);

typedef struct layout_builder
{
    int fill;
    uint32_t cap_elements, cap_attributes;  /* fill mode: sizes allocated from pass 1 */
    dui_layout *out;
    const uint8_t *data;
    uint32_t elements, attributes, text_runs, macros, binds;
    uint32_t open[DUI_XML_MAX_DEPTH + 1];   /* element index per depth */
    uint32_t last_child[DUI_XML_MAX_DEPTH + 1];
} layout_builder;

static dui_status
layout_sink(void *context, const dui_xml_event *ev)
{
    layout_builder *b = context;
    uint32_t d = ev->depth;

    switch (ev->kind)
    {
        case DUI_XML_ELEMENT_START:
            if (dui_name_equals(b->data, ev->name, "macro"))
                b->macros++;
            else if (dui_name_equals(b->data, ev->name, "bind"))
                b->binds++;
            if (b->fill)
            {
                dui_element *e;

                if (b->elements >= b->cap_elements)
                    return DUI_E_SYNTAX; /* the input changed after pass 1 (R-GEN-11) */
                e = &b->out->elements[b->elements];

                e->tag = ev->name;
                e->parent = d > 1 ? b->open[d - 1] : DUI_LAYOUT_NONE;
                e->first_child = DUI_LAYOUT_NONE;
                e->next = DUI_LAYOUT_NONE;
                e->first_attribute = b->attributes;
                e->attribute_count = 0;
                if (d > 1)
                {
                    dui_element *p = &b->out->elements[b->open[d - 1]];
                    if (p->first_child == DUI_LAYOUT_NONE)
                        p->first_child = b->elements;
                    else
                        b->out->elements[b->last_child[d - 1]].next = b->elements;
                    b->last_child[d - 1] = b->elements;
                }
            }
            b->open[d] = b->elements++;
            return DUI_OK;

        case DUI_XML_ATTRIBUTE:
            if (b->fill)
            {
                dui_attribute *a;

                if (b->attributes >= b->cap_attributes)
                    return DUI_E_SYNTAX;
                a = &b->out->attributes[b->attributes];
                a->name = ev->name;
                a->value = ev->value;
                b->out->elements[b->open[d]].attribute_count++;
            }
            b->attributes++;
            return DUI_OK;

        case DUI_XML_TEXT:
            b->text_runs++;
            return DUI_OK;

        case DUI_XML_ELEMENT_END:
            return DUI_OK;
    }
    return DUI_E_SYNTAX;
}

void
dui_layout_free(dui_layout *layout)
{
    if (!layout)
        return;
    free(layout->input);
    free(layout->elements);
    free(layout->attributes);
    memset(layout, 0, sizeof(*layout));
}

dui_status
dui_layout_load(const uint8_t *data, size_t size, dui_layout *out, size_t *error_offset)
{
    layout_builder count, fill;
    dui_status st;

    if (!out)
        return DUI_E_ARG;
    memset(out, 0, sizeof(*out));

    memset(&count, 0, sizeof(count));
    count.data = data;
    st = dui_xml_parse(data, size, layout_sink, &count, NULL, error_offset);
    if (st != DUI_OK)
        return st;
    if (dui_layout_test_between_passes)
        dui_layout_test_between_passes(data, size);

    out->input = malloc(size);
    out->elements = calloc(count.elements ? count.elements : 1, sizeof(dui_element));
    out->attributes = calloc(count.attributes ? count.attributes : 1, sizeof(dui_attribute));
    if (!out->input || !out->elements || !out->attributes)
    {
        dui_layout_free(out);
        return DUI_E_NO_MEMORY;
    }
    memcpy(out->input, data, size);
    out->input_size = size;

    memset(&fill, 0, sizeof(fill));
    fill.fill = 1;
    fill.cap_elements = count.elements;
    fill.cap_attributes = count.attributes;
    fill.out = out;
    fill.data = out->input;
    st = dui_xml_parse(out->input, size, layout_sink, &fill, NULL, error_offset);
    if (st != DUI_OK || fill.elements != count.elements || fill.attributes != count.attributes)
    {
        dui_layout_free(out);
        return st != DUI_OK ? st : DUI_E_SYNTAX;
    }
    out->element_count = fill.elements;
    out->attribute_count = fill.attributes;
    out->text_runs = fill.text_runs;
    out->macros = fill.macros;
    out->binds = fill.binds;
    return DUI_OK;
}
