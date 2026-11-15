/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Style sheets supplied by a theme (R-DUI-1..4, R-DUI-10)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Two passes over the same tokenizer (R-GEN-7): the first validates and counts without
 * allocating; only then is the model allocated, with sizes bounded by those counts, and the
 * second pass fills it. Both passes run the same code, so they cannot disagree.
 */

#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_sheet.h>
#include <tl4/directui/dui_xml.h>

static const char *const property_names[DUI_PROP_COUNT] =
{
    [DUI_PROP_BACKGROUND] = "background",
    [DUI_PROP_FOREGROUND] = "foreground",
    [DUI_PROP_FONT] = "font",
    [DUI_PROP_FONTSTYLE] = "fontstyle",
    [DUI_PROP_FONTSIZE] = "fontsize",
    [DUI_PROP_FONTWEIGHT] = "fontweight",
    [DUI_PROP_FONTFACE] = "fontface",
    [DUI_PROP_PADDING] = "padding",
    [DUI_PROP_MARGIN] = "margin",
    [DUI_PROP_PRESSEDPADDINGSHIFT] = "pressedpaddingshift",
    [DUI_PROP_CONTENTALIGN] = "contentalign",
    [DUI_PROP_HEIGHT] = "height",
    [DUI_PROP_WIDTH] = "width",
    [DUI_PROP_MINSIZE] = "minsize",
    [DUI_PROP_BORDERTHICKNESS] = "borderthickness",
    [DUI_PROP_BORDERCOLOR] = "bordercolor",
    [DUI_PROP_BORDERSTYLE] = "borderstyle",
    [DUI_PROP_TRANSPARENT] = "transparent",
};

/* Element states that act as conditions on a selector element (HYPOTHESIS on DirectUI
 * semantics, see the D0 notes; a condition cannot relabel or re-wire UI either way). */
static const char *const state_names[] =
{
    "keyfocused", "mousefocused", "pressed", "selected", "mousewithin", "windowactive",
    "hoveredover", "drophighlight", "checked", "expanded", "focused", "active",
};

static const char *const drop_content[] =
{
    "content", "tooltip", "shortcut", "accessible", "accname", "accrole", "accdefaction",
    "accdesc", "acchelp", "accitemtype", "accitemstatus", "accstate", "accvalue", "subtext",
};

static const char *const drop_identity[] =
{
    "id", "class", "layout", "layoutpos", "sheet", "resid",
};

static const char *const drop_behaviour[] =
{
    "enabled", "visible", "cursor", "active", "direction",
};

/* Tags that are content, never style-sheet structure (R-DUI-2). */
static const char *const forbidden_tags[] = { "macro", "bind" };

const char *
dui_property_name(dui_property property)
{
    return (unsigned)property < DUI_PROP_COUNT ? property_names[property] : "?";
}

#define IN_LIST(data, span, list) in_list((data), (span), (list), sizeof(list) / sizeof((list)[0]))

static int
in_list(const uint8_t *data, dui_span span, const char *const *list, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
    {
        if (dui_name_equals(data, span, list[i]))
            return (int)i + 1;
    }
    return 0;
}

enum level
{
    L_ROOT = 1,         /* duixml */
    L_SHEETS,           /* stylesheets */
    L_STYLE,            /* style */
    L_IF,               /* if */
    L_SELECTOR          /* selector element */
};

typedef struct builder
{
    const uint8_t *data;
    int fill;                       /* 0: validate and count; 1: fill the model */
    dui_sheets *out;
    dui_sheet_stats n;              /* running counts (indices in fill mode) */
    uint8_t level[DUI_XML_MAX_DEPTH + 1];
    uint32_t index[DUI_XML_MAX_DEPTH + 1];  /* scope or rule index per open element */
    int style_has_resid;
    dui_value value;
} builder;

static dui_status
add_condition(builder *b, dui_span name, dui_span value, uint8_t kind)
{
    dui_status st;

    /* condition values are checked as expressions, never evaluated here */
    st = dui_value_parse(b->data, b->out ? b->out->input_size : 0, value, &b->value);
    if (st != DUI_OK)
        return st;
    if (b->n.rules + b->n.conditions >= DUI_SHEET_MAX_RULES_AND_CONDS)
        return DUI_E_TOO_LARGE;
    if (b->fill)
    {
        dui_condition *c = &b->out->conditions[b->n.conditions];
        c->kind = kind;
        c->name = name;
        c->value = value;
    }
    b->n.conditions++;
    if (kind == DUI_COND_OTHER)
        b->n.unknown_conditions++;
    return DUI_OK;
}

static uint8_t
condition_kind(const uint8_t *data, dui_span name)
{
    if (dui_name_equals(data, name, "id"))
        return DUI_COND_ID;
    if (dui_name_equals(data, name, "class"))
        return DUI_COND_CLASS;
    if (IN_LIST(data, name, state_names))
        return DUI_COND_STATE;
    return DUI_COND_OTHER;
}

static dui_status
on_start(builder *b, const dui_xml_event *ev)
{
    uint32_t d = ev->depth;
    uint8_t parent = d > 1 ? b->level[d - 1] : 0;

    if (d == 1)
    {
        if (!dui_name_equals(b->data, ev->name, "duixml"))
            return DUI_E_FORBIDDEN;
        b->level[d] = L_ROOT;
        return DUI_OK;
    }
    if (parent == L_ROOT)
    {
        if (!dui_name_equals(b->data, ev->name, "stylesheets"))
            return DUI_E_FORBIDDEN; /* an element tree in theme input (R-DUI-2) */
        b->level[d] = L_SHEETS;
        return DUI_OK;
    }
    if (parent == L_SHEETS)
    {
        if (!dui_name_equals(b->data, ev->name, "style"))
            return DUI_E_FORBIDDEN;
        if (b->n.sheets >= DUI_SHEET_MAX_SHEETS)
            return DUI_E_TOO_LARGE;
        if (b->fill)
        {
            dui_sheet *s = &b->out->sheets[b->n.sheets];
            s->first_rule = b->n.rules;
            s->rule_count = 0;
        }
        b->index[d] = b->n.sheets++;
        b->level[d] = L_STYLE;
        b->style_has_resid = 0;
        return DUI_OK;
    }
    if (parent != L_STYLE && parent != L_IF && parent != L_SELECTOR)
        return DUI_E_FORBIDDEN;
    if (IN_LIST(b->data, ev->name, forbidden_tags))
        return DUI_E_FORBIDDEN;

    if (dui_name_equals(b->data, ev->name, "if"))
    {
        if (b->fill)
        {
            dui_scope *s = &b->out->scopes[b->n.scopes];
            s->parent_scope = DUI_SHEET_NONE;
            s->parent_rule = DUI_SHEET_NONE;
            if (parent == L_IF)
                s->parent_scope = b->index[d - 1];
            else if (parent == L_SELECTOR)
                s->parent_rule = b->index[d - 1];
            s->first_condition = b->n.conditions;
            s->condition_count = 0;
        }
        b->index[d] = b->n.scopes++;
        b->level[d] = L_IF;
        return DUI_OK;
    }

    /* a selector element */
    if (parent == L_SELECTOR)
        return DUI_E_FORBIDDEN; /* nested selectors are layout, not style */
    if (b->n.rules + b->n.conditions >= DUI_SHEET_MAX_RULES_AND_CONDS)
        return DUI_E_TOO_LARGE;
    if (b->fill)
    {
        dui_rule *r = &b->out->rules[b->n.rules];
        uint32_t k;

        r->selector = ev->name;
        r->scope = DUI_SHEET_NONE;
        for (k = d - 1; k > 0; k--)
        {
            if (b->level[k] == L_IF)
            {
                r->scope = b->index[k];
                break;
            }
            if (b->level[k] == L_STYLE)
                break;
        }
        for (k = d - 1; k > 0 && b->level[k] != L_STYLE; k--)
            ;
        r->sheet = b->index[k];
        b->out->sheets[r->sheet].rule_count++;
        r->first_condition = b->n.conditions;
        r->condition_count = 0;
        r->first_declaration = b->n.declarations;
        r->declaration_count = 0;
    }
    b->index[d] = b->n.rules++;
    b->level[d] = L_SELECTOR;
    return DUI_OK;
}

static dui_status
on_attribute(builder *b, const dui_xml_event *ev)
{
    uint8_t level = b->level[ev->depth];
    dui_status st;
    int prop;

    switch (level)
    {
        case L_ROOT:
        case L_SHEETS:
            return DUI_E_FORBIDDEN;

        case L_STYLE:
            if (!dui_name_equals(b->data, ev->name, "resid") || b->style_has_resid)
                return DUI_E_FORBIDDEN;
            if (ev->value.length == 0 || ev->value.length > DUI_XML_MAX_NAME_LEN)
                return DUI_E_SYNTAX;
            if (b->fill)
                b->out->sheets[b->index[ev->depth]].resid = ev->value;
            b->style_has_resid = 1;
            return DUI_OK;

        case L_IF:
            st = add_condition(b, ev->name, ev->value, condition_kind(b->data, ev->name));
            if (st == DUI_OK && b->fill)
                b->out->scopes[b->index[ev->depth]].condition_count++;
            return st;

        default:
            break;
    }

    /* selector element: state condition, allowed visual property, or dropped */
    if (IN_LIST(b->data, ev->name, state_names))
    {
        st = add_condition(b, ev->name, ev->value, DUI_COND_STATE);
        if (st == DUI_OK && b->fill)
            b->out->rules[b->index[ev->depth]].condition_count++;
        return st;
    }

    prop = IN_LIST(b->data, ev->name, property_names);
    if (!prop)
    {
        int reason;
        if (IN_LIST(b->data, ev->name, drop_content))
            reason = DUI_DROP_CONTENT;
        else if (IN_LIST(b->data, ev->name, drop_identity))
            reason = DUI_DROP_IDENTITY;
        else if (IN_LIST(b->data, ev->name, drop_behaviour))
            reason = DUI_DROP_BEHAVIOUR;
        else
            reason = DUI_DROP_UNLISTED;
        b->n.dropped++;
        b->n.dropped_by_reason[reason]++;
        return DUI_OK;
    }

    st = dui_value_parse(b->data, b->out ? b->out->input_size : 0, ev->value, &b->value);
    if (st != DUI_OK)
        return st;
    if (b->fill)
    {
        dui_declaration *decl = &b->out->declarations[b->n.declarations];
        uint16_t i;

        decl->property = (uint8_t)(prop - 1);
        decl->first_node = b->n.value_nodes;
        decl->node_count = b->value.count;
        for (i = 0; i < b->value.count; i++)
        {
            dui_value_node node = b->value.nodes[i];
            /* indices stay relative to the declaration's first node */
            b->out->nodes[b->n.value_nodes + i] = node;
        }
        b->out->rules[b->index[ev->depth]].declaration_count++;
    }
    b->n.declarations++;
    b->n.value_nodes += b->value.count;
    b->n.unknown_calls += b->value.unknown_calls;
    return DUI_OK;
}

static dui_status
sheet_sink(void *context, const dui_xml_event *ev)
{
    builder *b = context;

    switch (ev->kind)
    {
        case DUI_XML_ELEMENT_START:
            return on_start(b, ev);
        case DUI_XML_ATTRIBUTE:
            return on_attribute(b, ev);
        case DUI_XML_ELEMENT_END:
            if (b->level[ev->depth] == L_STYLE && !b->style_has_resid)
                return DUI_E_SYNTAX; /* <style> needs a resid */
            return DUI_OK;
        case DUI_XML_TEXT:
            /* between rules: meaningless, ignored and counted; inside a selector: content */
            if (b->level[ev->depth] == L_STYLE || b->level[ev->depth] == L_IF)
            {
                b->n.ignored_text++;
                return DUI_OK;
            }
            return DUI_E_FORBIDDEN;
    }
    return DUI_E_SYNTAX;
}

static void *
alloc_array(size_t count, size_t size)
{
    return calloc(count ? count : 1, size);
}

void
dui_sheets_free(dui_sheets *s)
{
    if (!s)
        return;
    free(s->input);
    free(s->sheets);
    free(s->scopes);
    free(s->rules);
    free(s->conditions);
    free(s->declarations);
    free(s->nodes);
    memset(s, 0, sizeof(*s));
}

dui_status
dui_sheets_load(const uint8_t *data, size_t size, dui_sheets *out, size_t *error_offset)
{
    builder *b;
    dui_sheets probe;
    dui_status st;

    if (!out)
        return DUI_E_ARG;
    memset(out, 0, sizeof(*out));

    /* The builder holds a 256-node value; keep it off the stack. Pass 1 needs no memory that
     * grows with the input, so this fixed block is the only allocation before validation. */
    b = calloc(1, sizeof(*b));
    if (!b)
        return DUI_E_NO_MEMORY;

    /* pass 1: validate and count */
    memset(&probe, 0, sizeof(probe));
    probe.input_size = size;
    b->data = data;
    b->out = &probe;
    st = dui_xml_parse(data, size, sheet_sink, b, NULL, error_offset);
    if (st == DUI_OK && b->n.sheets == 0)
        st = DUI_E_SYNTAX; /* no style sheet at all */
    if (st != DUI_OK)
    {
        free(b);
        return st;
    }

    /* allocate exactly what pass 1 counted */
    out->input = malloc(size);
    out->input_size = size;
    out->sheets = alloc_array(b->n.sheets, sizeof(dui_sheet));
    out->scopes = alloc_array(b->n.scopes, sizeof(dui_scope));
    out->rules = alloc_array(b->n.rules, sizeof(dui_rule));
    out->conditions = alloc_array(b->n.conditions, sizeof(dui_condition));
    out->declarations = alloc_array(b->n.declarations, sizeof(dui_declaration));
    out->nodes = alloc_array(b->n.value_nodes, sizeof(dui_value_node));
    if (!out->input || !out->sheets || !out->scopes || !out->rules || !out->conditions ||
        !out->declarations || !out->nodes)
    {
        dui_sheets_free(out);
        free(b);
        return DUI_E_NO_MEMORY;
    }
    memcpy(out->input, data, size);

    /* pass 2: fill, over the private copy */
    {
        dui_sheet_stats counted = b->n;

        memset(b, 0, sizeof(*b));
        b->data = out->input;
        b->fill = 1;
        b->out = out;
        st = dui_xml_parse(out->input, size, sheet_sink, b, NULL, error_offset);
        if (st != DUI_OK || memcmp(&counted, &b->n, sizeof(counted)) != 0)
        {
            dui_sheets_free(out);
            free(b);
            return st != DUI_OK ? st : DUI_E_SYNTAX;
        }
        out->stats = b->n;
    }
    free(b);
    return DUI_OK;
}
