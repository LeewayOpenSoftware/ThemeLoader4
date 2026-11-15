/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Layout resources owned by the consuming binary (R-DUI-4)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * This entry point is separate from dui_sheets_load and exists for resources that ship inside a
 * consuming binary, and for tests. No integration may feed it bytes that come from a style
 * (R-DUI-4). It builds a generic element tree: every tag is kept as an opaque name, attribute
 * values are kept raw (the consumer interprets them), and macro/bind are kept as plain elements;
 * nothing is expanded or evaluated (R-DUI-16).
 */

#ifndef TL4_DIRECTUI_DUI_LAYOUT_H
#define TL4_DIRECTUI_DUI_LAYOUT_H

#include <tl4/directui/dui.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUI_LAYOUT_NONE 0xFFFFFFFFu

typedef struct dui_attribute
{
    dui_span name;
    dui_span value;     /* raw, between the quotes */
} dui_attribute;

typedef struct dui_element
{
    dui_span tag;
    uint32_t parent;        /* DUI_LAYOUT_NONE for the root */
    uint32_t first_child;   /* DUI_LAYOUT_NONE if none */
    uint32_t next;          /* next sibling, DUI_LAYOUT_NONE if none */
    uint32_t first_attribute, attribute_count;
} dui_element;

typedef struct dui_layout
{
    uint8_t *input;             /* private copy; spans index it */
    size_t input_size;
    dui_element *elements;      /* element 0 is the root; document order */
    dui_attribute *attributes;
    uint32_t element_count, attribute_count;
    uint32_t text_runs;         /* text content: counted, not kept */
    uint32_t macros, binds;     /* kept as plain elements, never expanded */
} dui_layout;

/* Validate (no allocation), then build. On failure nothing is allocated and *out is zeroed. */
dui_status dui_layout_load(const uint8_t *data, size_t size, dui_layout *out, size_t *error_offset);
void dui_layout_free(dui_layout *layout);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_LAYOUT_H */
