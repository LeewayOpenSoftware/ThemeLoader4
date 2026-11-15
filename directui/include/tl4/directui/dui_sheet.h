/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Style sheets supplied by a theme (R-DUI-1..4, R-DUI-10)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * dui_sheets_load is the only entry point for theme input. It accepts
 *     <duixml> <stylesheets> <style resid="..."> rules </style> ... </stylesheets> </duixml>
 * and nothing else: an element tree, macro, bind or text inside a selector element is a reject
 * (R-DUI-2). Text directly inside <style> or <if> (between rules) has no meaning in a sheet; it is
 * ignored and counted, because the reference sheets contain such stray text.
 * A rule is a selector element (any tag, kept as an opaque name) inside zero or more <if>
 * conditions. Its attributes are:
 *   - visual properties from a fixed allow-list: parsed with dui_value_parse, kept (R-DUI-3);
 *   - element states (keyfocused, mousefocused, pressed, selected, mousewithin, ...): conditions;
 *   - anything else: dropped and counted, never kept (R-DUI-3).
 * There is no path from this entry point to an element tree.
 */

#ifndef TL4_DIRECTUI_DUI_SHEET_H
#define TL4_DIRECTUI_DUI_SHEET_H

#include <tl4/directui/dui_value.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DUI_SHEET_MAX_SHEETS            512u
#define DUI_SHEET_MAX_RULES_AND_CONDS   16384u

/* The visual properties a theme may set (R-DUI-3), measured on the stock style sheets. */
typedef enum dui_property
{
    DUI_PROP_BACKGROUND,
    DUI_PROP_FOREGROUND,
    DUI_PROP_FONT,
    DUI_PROP_FONTSTYLE,
    DUI_PROP_FONTSIZE,
    DUI_PROP_FONTWEIGHT,
    DUI_PROP_FONTFACE,
    DUI_PROP_PADDING,
    DUI_PROP_MARGIN,
    DUI_PROP_PRESSEDPADDINGSHIFT,
    DUI_PROP_CONTENTALIGN,
    DUI_PROP_HEIGHT,
    DUI_PROP_WIDTH,
    DUI_PROP_MINSIZE,
    DUI_PROP_BORDERTHICKNESS,
    DUI_PROP_BORDERCOLOR,
    DUI_PROP_BORDERSTYLE,
    DUI_PROP_TRANSPARENT,
    DUI_PROP_COUNT
} dui_property;

typedef enum dui_cond_kind
{
    DUI_COND_ID,        /* id="atom(...)" */
    DUI_COND_CLASS,     /* class="..." */
    DUI_COND_STATE,     /* a known element state (keyfocused, pressed, ...) */
    DUI_COND_OTHER      /* any other condition name: kept opaque and counted (R-DUI-10) */
} dui_cond_kind;

typedef struct dui_condition
{
    uint8_t kind;           /* dui_cond_kind */
    dui_span name;
    dui_span value;         /* raw value; checked with dui_value_parse */
} dui_condition;

typedef struct dui_declaration
{
    uint8_t property;       /* dui_property */
    uint16_t node_count;    /* nodes of the parsed value in the node pool */
    uint32_t first_node;
} dui_declaration;

#define DUI_SHEET_NONE 0xFFFFFFFFu

/* An <if> block: its conditions, inside an enclosing <if> or selector, if any. */
typedef struct dui_scope
{
    uint32_t parent_scope;  /* DUI_SHEET_NONE if none */
    uint32_t parent_rule;   /* DUI_SHEET_NONE if the <if> is not inside a selector element */
    uint32_t first_condition, condition_count;
} dui_scope;

typedef struct dui_rule
{
    dui_span selector;      /* tag of the selector element (opaque, folded on comparison) */
    uint32_t sheet;
    uint32_t scope;         /* innermost enclosing <if>, DUI_SHEET_NONE if none */
    uint32_t first_condition, condition_count;      /* the selector's own state conditions */
    uint32_t first_declaration, declaration_count;
} dui_rule;

typedef struct dui_sheet
{
    dui_span resid;
    uint32_t first_rule, rule_count;
} dui_sheet;

typedef struct dui_sheet_stats
{
    uint32_t sheets, scopes, rules, conditions, declarations, value_nodes;
    uint32_t unknown_conditions;            /* DUI_COND_OTHER */
    uint32_t unknown_calls;                 /* opaque function calls in kept values */
    uint32_t dropped;                       /* attributes dropped by R-DUI-3 */
    uint32_t dropped_by_reason[4];          /* see dui_drop_reason */
    uint32_t ignored_text;                  /* text runs between rules: ignored (reference sheets have them) */
} dui_sheet_stats;

typedef enum dui_drop_reason
{
    DUI_DROP_CONTENT,       /* content, tooltip, shortcut, acc*: text and accessibility */
    DUI_DROP_IDENTITY,      /* id, class, layout, layoutpos, sheet: identity and structure */
    DUI_DROP_BEHAVIOUR,     /* enabled, visible, cursor and other behaviour */
    DUI_DROP_UNLISTED       /* any other name not on the allow-list */
} dui_drop_reason;

/* The loaded model. Spans index `input`, a private copy of the resource. */
typedef struct dui_sheets
{
    uint8_t *input;
    size_t input_size;
    dui_sheet *sheets;
    dui_scope *scopes;
    dui_rule *rules;
    dui_condition *conditions;
    dui_declaration *declarations;
    dui_value_node *nodes;
    dui_sheet_stats stats;
} dui_sheets;

/* Validate (no allocation), then build. On failure nothing is allocated and *out is zeroed. */
dui_status dui_sheets_load(const uint8_t *data, size_t size, dui_sheets *out, size_t *error_offset);
void dui_sheets_free(dui_sheets *sheets);

const char *dui_property_name(dui_property property);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_SHEET_H */
