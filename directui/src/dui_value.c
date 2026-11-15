/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Value-expression parser for UIFILE attribute values (R-DUI-11..15)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Grammar (observed in the reference style sheets, directui/docs/D0-RESEARCH.md 3.4):
 *   value  := term
 *   term   := call | leaf
 *   call   := name '(' [ term { ',' term } ] ')'
 *   leaf   := int | int 'rp' | hex | name | 'string' | flags | fontspec
 *   flags  := simple { '|' simple }        simple := name | int | hex
 *   fontspec := name ';' int '%'
 * The parser is iterative with an explicit frame stack of DUI_VALUE_MAX_NESTING (R-GEN-6).
 * Known functions are checked for argument count and kind; unknown functions are kept as
 * opaque calls and counted (R-DUI-11). Nothing is evaluated here.
 */

#include <tl4/directui/dui_value.h>

/* Argument kinds for the function table. */
enum
{
    K_INT = 1u << 0,        /* INT */
    K_LEN = 1u << 1,        /* LENGTH */
    K_NAME = 1u << 2,       /* NAME */
    K_STR = 1u << 3,        /* STRING */
    K_FLAGS = 1u << 4,      /* FLAGS */
    K_CALL = 1u << 5,       /* any CALL */
    K_FONT = 1u << 6,       /* FONTSPEC */
    K_ANY = 0x7Fu
};

#define K_NUM (K_INT | K_LEN)
#define K_COLOR (K_NAME | K_CALL | K_INT)

typedef struct fn_spec
{
    const char *name;
    uint8_t min_args;
    uint8_t max_args;
    uint8_t kinds[8];       /* per position; the last listed kind applies to the rest */
    uint8_t nkinds;
} fn_spec;

static const fn_spec functions[DUI_FN_COUNT] =
{
    [DUI_FN_UNKNOWN]   = { "?", 0, 0, { 0 }, 0 },
    [DUI_FN_ATOM]      = { "atom", 1, 1, { K_NAME }, 1 },
    [DUI_FN_RECT]      = { "rect", 4, 4, { K_NUM }, 1 },
    [DUI_FN_SIZE]      = { "size", 2, 2, { K_NUM }, 1 },
    [DUI_FN_ARGB]      = { "argb", 4, 4, { K_INT }, 1 },
    [DUI_FN_RGB]       = { "rgb", 3, 3, { K_INT }, 1 },
    [DUI_FN_GTC]       = { "gtc", 4, 4, { K_NAME, K_INT }, 2 },
    [DUI_FN_GTF]       = { "gtf", 3, 3, { K_NAME, K_INT }, 2 },
    [DUI_FN_GTMAR]     = { "gtmar", 4, 4, { K_NAME, K_INT }, 2 },
    [DUI_FN_GTMET]     = { "gtmet", 4, 4, { K_NAME, K_INT }, 2 },
    [DUI_FN_DTB]       = { "dtb", 3, 3, { K_NAME, K_INT }, 2 },
    [DUI_FN_DFC]       = { "dfc", 2, 2, { K_INT, K_INT | K_FLAGS }, 2 },
    [DUI_FN_THEMEABLE] = { "themeable", 2, 2, { K_ANY }, 1 },
    [DUI_FN_GRADIENT]  = { "gradient", 4, 4, { K_COLOR, K_COLOR, K_COLOR, K_INT }, 4 },
    [DUI_FN_SYSMETRIC] = { "sysmetric", 1, 1, { K_INT }, 1 },
    [DUI_FN_RESSTR]    = { "resstr", 1, 2, { K_INT, K_CALL }, 2 },
    [DUI_FN_ICON]      = { "icon", 4, 6, { K_INT, K_NUM | K_CALL }, 2 },
    [DUI_FN_GRAPHIC]   = { "graphic", 2, 8, { K_INT, K_NUM | K_CALL }, 2 },
    [DUI_FN_LIBRARY]   = { "library", 1, 1, { K_NAME }, 1 },
    [DUI_FN_LAYOUT]    = { "*layout", 0, 5, { K_NUM | K_NAME }, 1 },
};

/* System metrics a UIFILE may ask for (R-DUI-15): the indices the reference sheets use. */
static const int32_t sysmetric_allow[] = { 10, 49, 50, 71, 72 };

typedef struct parser
{
    const uint8_t *d;
    size_t pos;
    size_t end;
    dui_value *v;
} parser;

static uint8_t
lower(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

static int
is_alpha(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int
is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}

static int
is_hex(uint8_t c)
{
    return is_digit(c) || (lower(c) >= 'a' && lower(c) <= 'f');
}

static void
skip_space(parser *p)
{
    while (p->pos < p->end && (p->d[p->pos] == ' ' || p->d[p->pos] == '\t' ||
                               p->d[p->pos] == '\r' || p->d[p->pos] == '\n'))
    {
        p->pos++;
    }
}

const char *
dui_function_name(dui_function function)
{
    return (unsigned)function < DUI_FN_COUNT ? functions[function].name : "?";
}

static int
span_equals_ci(const uint8_t *d, dui_span s, const char *name)
{
    uint32_t i;

    for (i = 0; i < s.length; i++)
    {
        if (name[i] == '\0' || lower(d[s.offset + i]) != (uint8_t)name[i])
            return 0;
    }
    return name[s.length] == '\0';
}

static dui_function
lookup_function(const uint8_t *d, dui_span name)
{
    unsigned i;

    for (i = 1; i < DUI_FN_COUNT; i++)
    {
        if (i != DUI_FN_LAYOUT && span_equals_ci(d, name, functions[i].name))
            return (dui_function)i;
    }
    if (name.length > 6)
    {
        dui_span tail = { name.offset + name.length - 6, 6 };
        if (span_equals_ci(d, tail, "layout"))
            return DUI_FN_LAYOUT;
    }
    return DUI_FN_UNKNOWN;
}

/* name := alpha {alnum|_} [ '::' alpha {alnum|_} ] { '.' alnum {alnum|_|-} } */
static dui_status
scan_name(parser *p, dui_span *out)
{
    size_t start = p->pos;
    int colons = 0;

    if (p->pos >= p->end || !is_alpha(p->d[p->pos]))
        return DUI_E_SYNTAX;
    for (;;)
    {
        while (p->pos < p->end && (is_alpha(p->d[p->pos]) || is_digit(p->d[p->pos])))
            p->pos++;
        if (p->pos + 1 < p->end && p->d[p->pos] == ':' && p->d[p->pos + 1] == ':' && !colons)
        {
            colons = 1;
            p->pos += 2;
            if (p->pos >= p->end || !is_alpha(p->d[p->pos]))
                return DUI_E_SYNTAX;
            continue;
        }
        if (p->pos + 1 < p->end && p->d[p->pos] == '.' &&
            (is_alpha(p->d[p->pos + 1]) || is_digit(p->d[p->pos + 1])))
        {
            p->pos++;
            continue;
        }
        break;
    }
    if (p->pos - start > 64)
        return DUI_E_TOO_LARGE;
    out->offset = (uint32_t)start;
    out->length = (uint32_t)(p->pos - start);
    return DUI_OK;
}

/* int := ['-'] digits | 0x hexdigits ; with overflow and range checks (R-DUI-12) */
static dui_status
scan_int(parser *p, int32_t *out, int *is_hex_literal)
{
    int64_t value = 0;
    int negative = 0;
    size_t digits = 0;

    *is_hex_literal = 0;
    if (p->pos < p->end && p->d[p->pos] == '-')
    {
        negative = 1;
        p->pos++;
    }
    if (!negative && p->pos + 1 < p->end && p->d[p->pos] == '0' && lower(p->d[p->pos + 1]) == 'x')
    {
        p->pos += 2;
        *is_hex_literal = 1;
        while (p->pos < p->end && is_hex(p->d[p->pos]))
        {
            uint8_t c = lower(p->d[p->pos]);
            value = value * 16 + (is_digit(c) ? c - '0' : c - 'a' + 10);
            if (value > DUI_VALUE_INT_LIMIT)
                return DUI_E_TOO_LARGE;
            p->pos++;
            digits++;
        }
    }
    else
    {
        while (p->pos < p->end && is_digit(p->d[p->pos]))
        {
            value = value * 10 + (p->d[p->pos] - '0');
            if (value > DUI_VALUE_INT_LIMIT)
                return DUI_E_TOO_LARGE;
            p->pos++;
            digits++;
        }
    }
    if (digits == 0)
        return DUI_E_SYNTAX;
    *out = (int32_t)(negative ? -value : value);
    return DUI_OK;
}

static dui_status
add_node(parser *p, uint16_t parent, uint16_t *prev_sibling, uint8_t kind, dui_span text,
         int32_t number, uint16_t *index)
{
    dui_value *v = p->v;
    dui_value_node *n;

    if (v->count >= DUI_VALUE_MAX_NODES)
        return DUI_E_TOO_LARGE;
    *index = v->count++;
    n = &v->nodes[*index];
    n->kind = kind;
    n->function = DUI_FN_UNKNOWN;
    n->parent = parent;
    n->first_child = DUI_VALUE_NO_NODE;
    n->next = DUI_VALUE_NO_NODE;
    n->child_count = 0;
    n->number = number;
    n->text = text;
    if (parent != DUI_VALUE_NO_NODE)
    {
        if (*prev_sibling == DUI_VALUE_NO_NODE)
            v->nodes[parent].first_child = *index;
        else
            v->nodes[*prev_sibling].next = *index;
        v->nodes[parent].child_count++;
        *prev_sibling = *index;
    }
    return DUI_OK;
}

/* A leaf (or the name of a call, signalled through *is_call). */
static dui_status
scan_term(parser *p, uint8_t *kind, dui_span *text, int32_t *number, int *is_call)
{
    size_t start = p->pos;
    dui_status st;
    uint8_t c;
    int hex;

    *is_call = 0;
    *number = 0;
    if (p->pos >= p->end)
        return DUI_E_SYNTAX;
    c = p->d[p->pos];

    if (c == '\'')
    {
        p->pos++;
        while (p->pos < p->end && p->d[p->pos] != '\'')
        {
            if (p->pos - start > DUI_VALUE_MAX_STRING)
                return DUI_E_TOO_LARGE;
            p->pos++;
        }
        if (p->pos >= p->end)
            return DUI_E_SYNTAX;
        p->pos++;
        *kind = DUI_V_STRING;
        text->offset = (uint32_t)start;
        text->length = (uint32_t)(p->pos - start);
        *number = (int32_t)(text->length - 2);
        return DUI_OK;
    }

    if (is_alpha(c))
    {
        st = scan_name(p, text);
        if (st != DUI_OK)
            return st;
        skip_space(p);
        if (p->pos < p->end && p->d[p->pos] == '(')
        {
            *is_call = 1;
            *kind = DUI_V_CALL;
            return DUI_OK;
        }
        if (p->pos < p->end && p->d[p->pos] == ';')
        {
            /* fontspec: name;N% */
            int32_t pct;
            p->pos++;
            skip_space(p);
            st = scan_int(p, &pct, &hex);
            if (st != DUI_OK)
                return st;
            if (hex || pct < 0 || p->pos >= p->end || p->d[p->pos] != '%')
                return DUI_E_SYNTAX;
            p->pos++;
            *kind = DUI_V_FONTSPEC;
            *number = pct;
            text->length = (uint32_t)(p->pos - text->offset);
            return DUI_OK;
        }
        *kind = DUI_V_NAME;
    }
    else if (is_digit(c) || c == '-')
    {
        st = scan_int(p, number, &hex);
        if (st != DUI_OK)
            return st;
        *kind = DUI_V_INT;
        if (!hex && p->pos + 1 < p->end && lower(p->d[p->pos]) == 'r' && lower(p->d[p->pos + 1]) == 'p')
        {
            p->pos += 2;
            *kind = DUI_V_LENGTH;
        }
        if (p->pos < p->end && (is_alpha(p->d[p->pos]) || is_digit(p->d[p->pos]) || p->d[p->pos] == '.'))
            return DUI_E_SYNTAX; /* other units, decimals: not in the dialect */
        text->offset = (uint32_t)start;
        text->length = (uint32_t)(p->pos - start);
    }
    else
    {
        return DUI_E_SYNTAX;
    }

    /* flags: simple { '|' simple } */
    skip_space(p);
    if (p->pos < p->end && p->d[p->pos] == '|' && *kind != DUI_V_LENGTH)
    {
        int32_t terms = 1;
        while (p->pos < p->end && p->d[p->pos] == '|')
        {
            dui_span dummy;
            int32_t num;

            p->pos++;
            skip_space(p);
            if (p->pos < p->end && is_alpha(p->d[p->pos]))
                st = scan_name(p, &dummy);
            else
                st = scan_int(p, &num, &hex);
            if (st != DUI_OK)
                return st;
            if (++terms > (int32_t)DUI_VALUE_MAX_ARGS)
                return DUI_E_TOO_LARGE;
            skip_space(p);
        }
        *kind = DUI_V_FLAGS;
        *number = terms;
        text->offset = (uint32_t)start;
        text->length = (uint32_t)(p->pos - start);
    }
    return DUI_OK;
}

static uint8_t
kind_bit(const dui_value_node *n)
{
    switch (n->kind)
    {
        case DUI_V_INT:      return K_INT;
        case DUI_V_LENGTH:   return K_LEN;
        case DUI_V_NAME:     return K_NAME;
        case DUI_V_STRING:   return K_STR;
        case DUI_V_FLAGS:    return K_FLAGS;
        case DUI_V_FONTSPEC: return K_FONT;
        default:             return K_CALL;
    }
}

static int
library_name_ok(const uint8_t *d, dui_span s)
{
    uint32_t i;

    /* R-DUI-13: at most 32 of [A-Za-z0-9_.-], no leading dot; names reach here without separators */
    if (s.length == 0 || s.length > DUI_VALUE_MAX_LIBRARY || d[s.offset] == '.')
        return 0;
    for (i = 0; i < s.length; i++)
    {
        uint8_t c = d[s.offset + i];
        if (!(is_alpha(c) || is_digit(c) || c == '.' || c == '-'))
            return 0;
    }
    return 1;
}

/* Semantic checks of the known functions, over the node array (no recursion). */
static dui_status
check_calls(const uint8_t *d, dui_value *v)
{
    uint16_t i;

    for (i = 0; i < v->count; i++)
    {
        dui_value_node *n = &v->nodes[i];
        const fn_spec *spec;
        uint16_t c, pos = 0;

        if (n->kind != DUI_V_CALL)
            continue;
        if (n->function == DUI_FN_UNKNOWN)
            continue;
        spec = &functions[n->function];
        if (n->child_count < spec->min_args || n->child_count > spec->max_args)
            return DUI_E_SYNTAX;
        for (c = n->first_child; c != DUI_VALUE_NO_NODE; c = v->nodes[c].next, pos++)
        {
            const dui_value_node *a = &v->nodes[c];
            uint8_t allowed = spec->kinds[pos < spec->nkinds ? pos : spec->nkinds - 1];

            if (!(kind_bit(a) & allowed))
                return DUI_E_SYNTAX;

            switch (n->function)
            {
                case DUI_FN_ARGB:
                case DUI_FN_RGB:
                    if (a->number < 0 || a->number > 255)
                        return DUI_E_TOO_LARGE;
                    break;
                case DUI_FN_GTC: case DUI_FN_GTF: case DUI_FN_GTMAR: case DUI_FN_GTMET: case DUI_FN_DTB:
                    /* R-DUI-15: class name, part and state 0..255, property id 1..65535 */
                    if (pos == 0 && a->text.length > DUI_VALUE_MAX_CLASS)
                        return DUI_E_TOO_LARGE;
                    if ((pos == 1 || pos == 2) && (a->number < 0 || a->number > 255))
                        return DUI_E_TOO_LARGE;
                    if (pos == 3 && (a->number < 1 || a->number > 65535))
                        return DUI_E_TOO_LARGE;
                    break;
                case DUI_FN_SYSMETRIC:
                {
                    size_t k;
                    int ok = 0;
                    for (k = 0; k < sizeof(sysmetric_allow) / sizeof(sysmetric_allow[0]); k++)
                        ok |= (a->number == sysmetric_allow[k]);
                    if (!ok)
                        return DUI_E_FORBIDDEN;
                    break;
                }
                case DUI_FN_RESSTR:
                case DUI_FN_ICON:
                case DUI_FN_GRAPHIC:
                    /* R-DUI-14: id 1..65535; the library reference, if any, is a library() call */
                    if (pos == 0 && (a->number < 1 || a->number > 65535))
                        return DUI_E_TOO_LARGE;
                    if (pos > 0 && a->kind == DUI_V_CALL &&
                        a->function != DUI_FN_LIBRARY && a->function != DUI_FN_SYSMETRIC)
                        return DUI_E_SYNTAX;
                    if (n->function != DUI_FN_RESSTR && pos > 0 && pos < 3 && a->kind != DUI_V_CALL &&
                        (a->number < 1 || a->number > 1024))
                        return DUI_E_TOO_LARGE;
                    break;
                case DUI_FN_LIBRARY:
                    if (!library_name_ok(d, a->text))
                        return DUI_E_FORBIDDEN;
                    break;
                default:
                    break;
            }
        }
        /* library() is only meaningful inside a resource reference */
        if (n->function == DUI_FN_LIBRARY &&
            (n->parent == DUI_VALUE_NO_NODE ||
             (v->nodes[n->parent].function != DUI_FN_RESSTR && v->nodes[n->parent].function != DUI_FN_ICON &&
              v->nodes[n->parent].function != DUI_FN_GRAPHIC)))
        {
            return DUI_E_SYNTAX;
        }
    }
    return DUI_OK;
}

dui_status
dui_value_parse(const uint8_t *data, size_t size, dui_span value, dui_value *out)
{
    struct frame { uint16_t call; uint16_t last; } stack[DUI_VALUE_MAX_NESTING];
    uint32_t depth = 0;
    uint16_t root_prev = DUI_VALUE_NO_NODE;
    int expect_value = 1;
    int have_root = 0;
    parser p;
    dui_status st;

    if (!data || !out || (size_t)value.offset > size || (size_t)value.length > size - value.offset)
        return DUI_E_ARG;

    out->count = 0;
    out->unknown_calls = 0;
    p.d = data;
    p.pos = value.offset;
    p.end = (size_t)value.offset + value.length;
    p.v = out;

    for (;;)
    {
        skip_space(&p);
        if (expect_value)
        {
            uint8_t kind;
            dui_span text = { 0, 0 };
            int32_t number;
            int is_call;
            uint16_t parent = depth ? stack[depth - 1].call : DUI_VALUE_NO_NODE;
            uint16_t *prev = depth ? &stack[depth - 1].last : &root_prev;
            uint16_t index;

            /* empty argument list: "name()" */
            if (depth && p.pos < p.end && p.d[p.pos] == ')' &&
                out->nodes[stack[depth - 1].call].child_count == 0)
            {
                p.pos++;
                depth--;
                expect_value = 0;
                continue;
            }
            if (!depth && have_root)
                return DUI_E_SYNTAX;
            st = scan_term(&p, &kind, &text, &number, &is_call);
            if (st != DUI_OK)
                return st;
            st = add_node(&p, parent, prev, kind, text, number, &index);
            if (st != DUI_OK)
                return st;
            if (parent != DUI_VALUE_NO_NODE && out->nodes[parent].child_count > DUI_VALUE_MAX_ARGS)
                return DUI_E_TOO_LARGE;
            have_root = 1;
            if (is_call)
            {
                dui_function fn = lookup_function(data, text);
                out->nodes[index].function = (uint8_t)fn;
                if (fn == DUI_FN_UNKNOWN)
                    out->unknown_calls++;
                if (depth >= DUI_VALUE_MAX_NESTING)
                    return DUI_E_DEPTH;
                stack[depth].call = index;
                stack[depth].last = DUI_VALUE_NO_NODE;
                depth++;
                p.pos++; /* '(' */
                expect_value = 1;
            }
            else
            {
                expect_value = 0;
            }
            continue;
        }

        /* after a value */
        if (p.pos >= p.end)
            break;
        if (!depth)
            return DUI_E_SYNTAX; /* trailing input after the value */
        if (p.d[p.pos] == ',')
        {
            p.pos++;
            expect_value = 1;
            skip_space(&p);
            if (p.pos < p.end && p.d[p.pos] == ')')
                return DUI_E_SYNTAX; /* "f(a,)" */
            continue;
        }
        if (p.d[p.pos] == ')')
        {
            p.pos++;
            depth--;
            continue;
        }
        return DUI_E_SYNTAX;
    }

    if (depth || !have_root || expect_value)
        return DUI_E_SYNTAX;
    return check_calls(data, out);
}
