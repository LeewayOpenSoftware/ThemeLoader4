/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Value-expression parser for UIFILE attribute values (R-DUI-11..15)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#ifndef TL4_DIRECTUI_DUI_VALUE_H
#define TL4_DIRECTUI_DUI_VALUE_H

#include <tl4/directui/dui.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Limits per value (REQUIREMENTS.md 9.5, R-DUI-12, R-DUI-13). */
#define DUI_VALUE_MAX_NODES     256u
#define DUI_VALUE_MAX_NESTING   8u
#define DUI_VALUE_MAX_ARGS      16u
#define DUI_VALUE_INT_LIMIT     1000000
#define DUI_VALUE_MAX_STRING    256u
#define DUI_VALUE_MAX_CLASS     59u
#define DUI_VALUE_MAX_LIBRARY   32u
#define DUI_VALUE_NO_NODE       0xFFFFu

typedef enum dui_value_kind
{
    DUI_V_INT,          /* number: decimal or 0x hex, within +/- DUI_VALUE_INT_LIMIT */
    DUI_V_LENGTH,       /* number with the "rp" unit (relative pixels) */
    DUI_V_NAME,         /* identifier, possibly App::Class or dotted (a.b) */
    DUI_V_STRING,       /* 'text' without escapes; number = length */
    DUI_V_FLAGS,        /* a | b | c (names or integers); number = term count */
    DUI_V_FONTSPEC,     /* name;N% ; number = percentage */
    DUI_V_CALL          /* name(args); function = dui_function id or DUI_FN_UNKNOWN */
} dui_value_kind;

typedef enum dui_function
{
    DUI_FN_UNKNOWN = 0,
    DUI_FN_ATOM, DUI_FN_RECT, DUI_FN_SIZE, DUI_FN_ARGB, DUI_FN_RGB,
    DUI_FN_GTC, DUI_FN_GTF, DUI_FN_GTMAR, DUI_FN_GTMET, DUI_FN_DTB, DUI_FN_DFC,
    DUI_FN_THEMEABLE, DUI_FN_GRADIENT, DUI_FN_SYSMETRIC,
    DUI_FN_RESSTR, DUI_FN_ICON, DUI_FN_GRAPHIC, DUI_FN_LIBRARY,
    DUI_FN_LAYOUT,      /* any built-in or consumer layout: name ends in "layout" */
    DUI_FN_COUNT
} dui_function;

typedef struct dui_value_node
{
    uint8_t kind;           /* dui_value_kind */
    uint8_t function;       /* dui_function, for DUI_V_CALL */
    uint16_t parent;        /* DUI_VALUE_NO_NODE for the root */
    uint16_t first_child;   /* DUI_VALUE_NO_NODE if none */
    uint16_t next;          /* next sibling, DUI_VALUE_NO_NODE if none */
    uint16_t child_count;
    int32_t number;         /* value for INT/LENGTH/FONTSPEC; count for FLAGS/STRING */
    dui_span text;          /* span of the node in the input (name for CALL) */
} dui_value_node;

typedef struct dui_value
{
    uint16_t count;             /* nodes used; node 0 is the root */
    uint16_t unknown_calls;     /* calls to functions not in the table (kept, never evaluated) */
    dui_value_node nodes[DUI_VALUE_MAX_NODES];
} dui_value;

/* Parse and check one attribute value (span of data). No allocation, no recursion. */
dui_status dui_value_parse(const uint8_t *data, size_t size, dui_span value, dui_value *out);

/* Name of a function id, for diagnostics. */
const char *dui_function_name(dui_function function);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_VALUE_H */
