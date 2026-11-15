/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Style sheets supplied by a theme (R-DUI-1..4, R-DUI-10)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Two passes over the same tokenizer (R-GEN-7): the first validates and counts without
 * allocating; only then is the model allocated, with sizes bounded by those counts, and the
 * second pass fills it. The input may change between and during the passes (R-GEN-11), so the
 * second pass checks every write against the allocated sizes before it writes; a difference is
 * a reject.
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
    "enabled", "visible", "cursor", "direction",
};

/* R-DUI-3a: size-bearing properties take non-negative values only. */
static int
is_nonnegative_property(int property)
{
    switch (property)
    {
        case DUI_PROP_HEIGHT: case DUI_PROP_WIDTH: case DUI_PROP_MINSIZE: case DUI_PROP_PADDING:
        case DUI_PROP_MARGIN: case DUI_PROP_BORDERTHICKNESS: case DUI_PROP_FONTSIZE:
            return 1;
        default:
            return 0;
    }
}

/* Test hook (R-GEN-11 test): called between the passes with the caller's buffer. */
#ifdef DUI_TESTING
/* Only in the test build of the core: a shipped core has no writable hook. */
void (*dui_sheets_test_between_passes)(const uint8_t *data, size_t size);
#endif

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
    dui_sheet_stats cap;            /* fill mode: the sizes allocated from pass 1 */
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
        dui_condition *c;

        if (b->n.conditions >= b->cap.conditions)
            return DUI_E_SYNTAX; /* the input changed after pass 1 (R-GEN-11) */
        c = &b->out->conditions[b->n.conditions];
        c->kind = kind;
        c->name = name;
        c->value = value;
    }
    b->n.conditions++;
    if (kind == DUI_COND_OTHER)
        b->n.unknown_conditions++;
    return DUI_OK;
}

/* resid: a name, as R-DUI-9 (letter or '_' first, then letters, digits, '_', '.', '-'). */
static int
resid_ok(const uint8_t *data, dui_span value)
{
    uint32_t i;

    if (value.length == 0 || value.length > DUI_XML_MAX_NAME_LEN)
        return 0;
    for (i = 0; i < value.length; i++)
    {
        uint8_t c = data[value.offset + i];
        int alpha = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        int other = (c >= '0' && c <= '9') || c == '.' || c == '-';
        if (!(alpha || (i > 0 && other)))
            return 0;
    }
    return 1;
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
            dui_sheet *s;

            if (b->n.sheets >= b->cap.sheets)
                return DUI_E_SYNTAX;
            s = &b->out->sheets[b->n.sheets];
            s->first_rule = b->n.rules;
            s->rule_count = 0;
        }
        b->index[d] = b->n.sheets++;
        b->level[d] = L_STYLE;
        b->style_has_resid = 0;
        return DUI_OK;
    }
    if (parent == L_SELECTOR)
        return DUI_E_FORBIDDEN; /* a selector has no children in a sheet */
    if (parent != L_STYLE && parent != L_IF)
        return DUI_E_FORBIDDEN;
    if (IN_LIST(b->data, ev->name, forbidden_tags))
        return DUI_E_FORBIDDEN;

    if (dui_name_equals(b->data, ev->name, "if"))
    {
        if (b->n.scopes >= DUI_SHEET_MAX_SCOPES)
            return DUI_E_TOO_LARGE;
        if (b->fill)
        {
            dui_scope *s;

            if (b->n.scopes >= b->cap.scopes)
                return DUI_E_SYNTAX;
            s = &b->out->scopes[b->n.scopes];
            s->parent_scope = (parent == L_IF) ? b->index[d - 1] : DUI_SHEET_NONE;
            s->first_condition = b->n.conditions;
            s->condition_count = 0;
        }
        b->index[d] = b->n.scopes++;
        b->level[d] = L_IF;
        return DUI_OK;
    }

    /* a selector element */
    if (b->n.rules + b->n.conditions >= DUI_SHEET_MAX_RULES_AND_CONDS)
        return DUI_E_TOO_LARGE;
    if (b->fill)
    {
        dui_rule *r;
        uint32_t k;

        if (b->n.rules >= b->cap.rules)
            return DUI_E_SYNTAX;
        r = &b->out->rules[b->n.rules];

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
        if (r->sheet >= b->cap.sheets)
            return DUI_E_SYNTAX;
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
            if (!resid_ok(b->data, ev->value))
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
    if (is_nonnegative_property(prop - 1))
    {
        uint16_t i;
        for (i = 0; i < b->value.count; i++)
        {
            const dui_value_node *vn = &b->value.nodes[i];
            if ((vn->kind == DUI_V_INT || vn->kind == DUI_V_LENGTH) && vn->number < 0)
                return DUI_E_FORBIDDEN; /* R-DUI-3a */
        }
    }
    if (b->n.declarations >= DUI_SHEET_MAX_DECLARATIONS ||
        b->n.value_nodes > DUI_SHEET_MAX_VALUE_NODES - b->value.count)
    {
        return DUI_E_TOO_LARGE;
    }
    if (b->fill)
    {
        dui_declaration *decl;
        uint16_t i;

        if (b->n.declarations >= b->cap.declarations ||
            b->value.count > b->cap.value_nodes || b->n.value_nodes > b->cap.value_nodes - b->value.count)
        {
            return DUI_E_SYNTAX; /* the input changed after pass 1 (R-GEN-11) */
        }
        decl = &b->out->declarations[b->n.declarations];

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
#ifdef DUI_TESTING
    if (dui_sheets_test_between_passes)
        dui_sheets_test_between_passes(data, size);
#endif

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
        b->cap = counted;
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
