/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Bounded tokenizer for the UIFILE XML subset (R-DUI-5..10)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Accepted: elements, attributes (single or double quotes), comments, text, the five predefined
 * entity references. Rejected: NUL, invalid UTF-8, DOCTYPE and any other "<!" declaration,
 * processing instructions (the XML declaration too), CDATA, character references, other
 * entity references, mismatched or unterminated constructs, duplicate attributes, content after
 * the root. The walk is iterative; the open-element stack is a fixed array of DUI_XML_MAX_DEPTH.
 */

#include <tl4/directui/dui_xml.h>

typedef struct xml_state
{
    const uint8_t *data;
    size_t size;
    size_t pos;
    dui_xml_sink sink;
    void *context;
    dui_xml_stats stats;
    dui_span open[DUI_XML_MAX_DEPTH];
    uint32_t depth;
} xml_state;

static int
is_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int
is_name_start(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int
is_name_char(uint8_t c)
{
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

static uint8_t
fold(uint8_t c)
{
    return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + ('a' - 'A')) : c;
}

int
dui_name_equals(const uint8_t *data, dui_span span, const char *name)
{
    uint32_t i;

    for (i = 0; i < span.length; i++)
    {
        if (name[i] == '\0' || fold(data[span.offset + i]) != fold((uint8_t)name[i]))
            return 0;
    }
    return name[span.length] == '\0';
}

int
dui_spans_equal(const uint8_t *data, dui_span a, dui_span b)
{
    uint32_t i;

    if (a.length != b.length)
        return 0;
    for (i = 0; i < a.length; i++)
    {
        if (fold(data[a.offset + i]) != fold(data[b.offset + i]))
            return 0;
    }
    return 1;
}

/* Strict UTF-8 over the whole input, and no NUL (R-DUI-6). */
static dui_status
check_encoding(const uint8_t *data, size_t size, size_t *error_offset)
{
    size_t i = 0;

    while (i < size)
    {
        uint8_t c = data[i];
        size_t need;
        uint32_t cp, min;

        if (c == 0)
        {
            *error_offset = i;
            return DUI_E_FORBIDDEN;
        }
        if (c < 0x80)
        {
            i++;
            continue;
        }
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1Fu; min = 0x80; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0Fu; min = 0x800; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07u; min = 0x10000; }
        else
        {
            *error_offset = i;
            return DUI_E_SYNTAX;
        }
        if (need > size - i - 1)
        {
            *error_offset = i;
            return DUI_E_SYNTAX;
        }
        for (size_t k = 1; k <= need; k++)
        {
            uint8_t cc = data[i + k];
            if ((cc & 0xC0) != 0x80)
            {
                *error_offset = i + k;
                return DUI_E_SYNTAX;
            }
            cp = (cp << 6) | (uint32_t)(cc & 0x3Fu);
        }
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            *error_offset = i;
            return DUI_E_SYNTAX;
        }
        i += need + 1;
    }
    return DUI_OK;
}

static int
starts_with(const xml_state *s, const char *text)
{
    size_t i;

    for (i = 0; text[i] != '\0'; i++)
    {
        if (s->pos + i >= s->size || s->data[s->pos + i] != (uint8_t)text[i])
            return 0;
    }
    return 1;
}

static void
skip_space(xml_state *s)
{
    while (s->pos < s->size && is_space(s->data[s->pos]))
        s->pos++;
}

static dui_status
emit(xml_state *s, dui_xml_event_kind kind, dui_span name, dui_span value)
{
    dui_xml_event ev;

    if (!s->sink)
        return DUI_OK;
    ev.kind = kind;
    ev.name = name;
    ev.value = value;
    ev.depth = s->depth;
    return s->sink(s->context, &ev);
}

static dui_status
read_name(xml_state *s, dui_span *name)
{
    size_t start = s->pos;

    if (s->pos >= s->size || !is_name_start(s->data[s->pos]))
        return DUI_E_SYNTAX;
    while (s->pos < s->size && is_name_char(s->data[s->pos]))
    {
        if (s->pos - start >= DUI_XML_MAX_NAME_LEN)
            return DUI_E_TOO_LARGE;
        s->pos++;
    }
    name->offset = (uint32_t)start;
    name->length = (uint32_t)(s->pos - start);
    if (name->length > s->stats.max_name_len)
        s->stats.max_name_len = name->length;
    return DUI_OK;
}

/* At '&': accept only the five predefined entity references (R-DUI-7). */
static dui_status
check_entity(xml_state *s)
{
    static const char *const names[] = { "&lt;", "&gt;", "&amp;", "&quot;", "&apos;" };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        if (starts_with(s, names[i]))
        {
            size_t k = 0;
            while (names[i][k] != '\0')
                k++;
            s->pos += k;
            return DUI_OK;
        }
    }
    return DUI_E_FORBIDDEN;
}

static dui_status
read_value(xml_state *s, dui_span *value)
{
    uint8_t quote;
    size_t start;
    dui_status st;

    if (s->pos >= s->size || (s->data[s->pos] != '"' && s->data[s->pos] != '\''))
        return DUI_E_SYNTAX;
    quote = s->data[s->pos++];
    start = s->pos;
    for (;;)
    {
        uint8_t c;

        if (s->pos >= s->size)
            return DUI_E_SYNTAX;
        c = s->data[s->pos];
        if (c == quote)
            break;
        if (c == '<')
            return DUI_E_SYNTAX;
        if (c == '&')
        {
            st = check_entity(s);
            if (st != DUI_OK)
                return st;
        }
        else
        {
            s->pos++;
        }
        if (s->pos - start > DUI_XML_MAX_VALUE_LEN)
            return DUI_E_TOO_LARGE;
    }
    value->offset = (uint32_t)start;
    value->length = (uint32_t)(s->pos - start);
    if (value->length > s->stats.max_value_len)
        s->stats.max_value_len = value->length;
    s->pos++; /* closing quote */
    return DUI_OK;
}

static dui_status
read_comment(xml_state *s)
{
    s->pos += 4; /* "<!--" */
    for (;;)
    {
        if (s->pos + 1 >= s->size)
            return DUI_E_SYNTAX;
        if (s->data[s->pos] == '-' && s->data[s->pos + 1] == '-')
        {
            /* "--" is only allowed as part of the terminator */
            if (s->pos + 2 < s->size && s->data[s->pos + 2] == '>')
            {
                s->pos += 3;
                s->stats.comments++;
                return DUI_OK;
            }
            return DUI_E_SYNTAX;
        }
        s->pos++;
    }
}

/* At '<' followed by a name start. */
static dui_status
read_start_tag(xml_state *s)
{
    dui_span name, attrs[DUI_XML_MAX_ATTRS_PER_ELEM];
    uint32_t nattrs = 0;
    dui_span none = { 0, 0 };
    dui_status st;

    s->pos++; /* '<' */
    st = read_name(s, &name);
    if (st != DUI_OK)
        return st;
    if (s->depth >= DUI_XML_MAX_DEPTH)
        return DUI_E_DEPTH;
    if (s->stats.elements >= DUI_XML_MAX_ELEMENTS)
        return DUI_E_TOO_LARGE;
    s->stats.elements++;
    s->open[s->depth++] = name;
    if (s->depth > s->stats.max_depth)
        s->stats.max_depth = s->depth;
    st = emit(s, DUI_XML_ELEMENT_START, name, none);
    if (st != DUI_OK)
        return st;

    for (;;)
    {
        size_t before = s->pos;
        dui_span aname, avalue;
        uint32_t i;

        skip_space(s);
        if (s->pos >= s->size)
            return DUI_E_SYNTAX;
        if (s->data[s->pos] == '>')
        {
            s->pos++;
            return DUI_OK;
        }
        if (s->data[s->pos] == '/')
        {
            if (s->pos + 1 >= s->size || s->data[s->pos + 1] != '>')
                return DUI_E_SYNTAX;
            s->pos += 2;
            st = emit(s, DUI_XML_ELEMENT_END, name, none);
            s->depth--;
            return st;
        }
        if (s->pos == before)
            return DUI_E_SYNTAX; /* attributes must be separated by white space */
        st = read_name(s, &aname);
        if (st != DUI_OK)
            return st;
        skip_space(s);
        if (s->pos >= s->size || s->data[s->pos] != '=')
            return DUI_E_SYNTAX;
        s->pos++;
        skip_space(s);
        st = read_value(s, &avalue);
        if (st != DUI_OK)
            return st;
        for (i = 0; i < nattrs; i++)
        {
            if (dui_spans_equal(s->data, attrs[i], aname))
                return DUI_E_SYNTAX; /* duplicate attribute (R-DUI-8) */
        }
        if (nattrs >= DUI_XML_MAX_ATTRS_PER_ELEM || s->stats.attributes >= DUI_XML_MAX_ATTRS_TOTAL)
            return DUI_E_TOO_LARGE;
        attrs[nattrs++] = aname;
        s->stats.attributes++;
        if (nattrs > s->stats.max_attrs_per_element)
            s->stats.max_attrs_per_element = nattrs;
        st = emit(s, DUI_XML_ATTRIBUTE, aname, avalue);
        if (st != DUI_OK)
            return st;
    }
}

/* At "</". */
static dui_status
read_end_tag(xml_state *s)
{
    dui_span name, none = { 0, 0 };
    dui_status st;

    s->pos += 2;
    st = read_name(s, &name);
    if (st != DUI_OK)
        return st;
    skip_space(s);
    if (s->pos >= s->size || s->data[s->pos] != '>')
        return DUI_E_SYNTAX;
    s->pos++;
    if (s->depth == 0 || !dui_spans_equal(s->data, s->open[s->depth - 1], name))
        return DUI_E_SYNTAX;
    st = emit(s, DUI_XML_ELEMENT_END, name, none);
    s->depth--;
    return st;
}

/* Text between tags, up to the next '<'. */
static dui_status
read_text(xml_state *s, int in_root)
{
    size_t start = s->pos;
    int significant = 0;
    dui_status st;

    while (s->pos < s->size && s->data[s->pos] != '<')
    {
        uint8_t c = s->data[s->pos];

        if (c == '&')
        {
            st = check_entity(s);
            if (st != DUI_OK)
                return st;
            significant = 1;
            continue;
        }
        if (c == '>')
            return DUI_E_SYNTAX; /* a stray '>' is legal XML only after "]]"; keep the subset strict */
        if (!is_space(c))
            significant = 1;
        s->pos++;
    }
    if (!significant)
        return DUI_OK;
    if (!in_root)
        return DUI_E_SYNTAX; /* text outside the root element */
    {
        dui_span none = { 0, 0 };
        dui_span value;

        if (s->pos - start > DUI_XML_MAX_VALUE_LEN)
            return DUI_E_TOO_LARGE;
        value.offset = (uint32_t)start;
        value.length = (uint32_t)(s->pos - start);
        s->stats.text_runs++;
        return emit(s, DUI_XML_TEXT, none, value);
    }
}

dui_status
dui_xml_parse(const uint8_t *data, size_t size,
              dui_xml_sink sink, void *context,
              dui_xml_stats *stats, size_t *error_offset)
{
    xml_state s;
    size_t dummy_offset;
    int seen_root = 0;
    dui_status st;

    if (!error_offset)
        error_offset = &dummy_offset;
    *error_offset = 0;
    if (!data || size == 0)
        return DUI_E_ARG;
    if (size > DUI_XML_MAX_SIZE)
        return DUI_E_TOO_LARGE;

    st = check_encoding(data, size, error_offset);
    if (st != DUI_OK)
        return st;

    s.data = data;
    s.size = size;
    s.pos = 0;
    s.sink = sink;
    s.context = context;
    s.depth = 0;
    s.stats = (dui_xml_stats){ 0 };

    while (s.pos < s.size)
    {
        if (s.data[s.pos] != '<')
        {
            st = read_text(&s, s.depth > 0);
        }
        else if (starts_with(&s, "<!--"))
        {
            st = read_comment(&s);
        }
        else if (starts_with(&s, "<!") || starts_with(&s, "<?"))
        {
            st = DUI_E_FORBIDDEN; /* DOCTYPE, declarations, CDATA, processing instructions */
        }
        else if (starts_with(&s, "</"))
        {
            st = read_end_tag(&s);
        }
        else if (s.depth == 0 && seen_root)
        {
            st = DUI_E_SYNTAX; /* a second root element */
        }
        else
        {
            seen_root = 1;
            st = read_start_tag(&s);
        }
        if (st != DUI_OK)
        {
            *error_offset = s.pos;
            return st;
        }
    }

    if (!seen_root || s.depth != 0)
    {
        *error_offset = s.pos;
        return DUI_E_SYNTAX;
    }
    if (stats)
        *stats = s.stats;
    return DUI_OK;
}
