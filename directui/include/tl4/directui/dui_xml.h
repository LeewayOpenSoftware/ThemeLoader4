/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Bounded tokenizer for the UIFILE XML subset (R-DUI-5..10)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#ifndef TL4_DIRECTUI_DUI_XML_H
#define TL4_DIRECTUI_DUI_XML_H

#include <tl4/directui/dui.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Limits per UIFILE resource (REQUIREMENTS.md 9.5). */
#define DUI_XML_MAX_SIZE            (512u * 1024u)
#define DUI_XML_MAX_DEPTH           32u
#define DUI_XML_MAX_ELEMENTS        16384u
#define DUI_XML_MAX_ATTRS_PER_ELEM  64u
#define DUI_XML_MAX_ATTRS_TOTAL     65536u
#define DUI_XML_MAX_NAME_LEN        64u
#define DUI_XML_MAX_VALUE_LEN       4096u

typedef enum dui_xml_event_kind
{
    DUI_XML_ELEMENT_START,  /* name = tag; depth = depth of the element (root is 1) */
    DUI_XML_ATTRIBUTE,      /* name = attribute name; value = raw value between the quotes */
    DUI_XML_ELEMENT_END,    /* name = tag; also sent for an empty-element tag */
    DUI_XML_TEXT            /* value = raw text that is not only white space */
} dui_xml_event_kind;

typedef struct dui_xml_event
{
    dui_xml_event_kind kind;
    dui_span name;
    dui_span value;
    uint32_t depth;
} dui_xml_event;

/* Called for every event in document order. A status other than DUI_OK stops the parse and is
 * returned by dui_xml_parse. Spans index the caller's input; values are raw (entity references
 * are validated but not replaced). */
typedef dui_status (*dui_xml_sink)(void *context, const dui_xml_event *event);

typedef struct dui_xml_stats
{
    uint32_t elements;
    uint32_t attributes;
    uint32_t max_depth;
    uint32_t max_attrs_per_element;
    uint32_t max_name_len;
    uint32_t max_value_len;
    uint32_t comments;
    uint32_t text_runs;
} dui_xml_stats;

/* Validate and tokenize one UIFILE resource. No allocation, no recursion. sink may be NULL
 * (validation only); stats may be NULL. On failure, *error_offset (if not NULL) is the offset
 * at which the input was rejected. */
dui_status dui_xml_parse(const uint8_t *data, size_t size,
                         dui_xml_sink sink, void *context,
                         dui_xml_stats *stats, size_t *error_offset);

/* Ordinal ASCII case-insensitive comparison of an input span with a NUL-terminated ASCII name. */
int dui_name_equals(const uint8_t *data, dui_span span, const char *name);

/* Ordinal ASCII case-insensitive comparison of two input spans. */
int dui_spans_equal(const uint8_t *data, dui_span a, dui_span b);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_XML_H */
