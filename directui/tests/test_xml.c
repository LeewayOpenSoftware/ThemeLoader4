/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests of the UIFILE XML tokenizer; every fixture is written here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_xml.h>

static int failures;

static void
expect(const char *what, const char *xml, dui_status expected)
{
    size_t off = 0;
    dui_status st = dui_xml_parse((const uint8_t *)xml, strlen(xml), NULL, NULL, NULL, &off);

    if (st != expected)
    {
        fprintf(stderr, "FAIL %s: got %s, expected %s (offset %zu)\n",
                what, dui_status_name(st), dui_status_name(expected), off);
        failures++;
    }
}

static void
expect_bytes(const char *what, const uint8_t *data, size_t size, dui_status expected)
{
    dui_status st = dui_xml_parse(data, size, NULL, NULL, NULL, NULL);

    if (st != expected)
    {
        fprintf(stderr, "FAIL %s: got %s, expected %s\n", what, dui_status_name(st), dui_status_name(expected));
        failures++;
    }
}

typedef struct counter
{
    unsigned starts, ends, attrs, texts;
    unsigned max_depth;
} counter;

static dui_status
count_events(void *context, const dui_xml_event *ev)
{
    counter *c = context;

    switch (ev->kind)
    {
        case DUI_XML_ELEMENT_START: c->starts++; break;
        case DUI_XML_ELEMENT_END:   c->ends++;   break;
        case DUI_XML_ATTRIBUTE:     c->attrs++;  break;
        case DUI_XML_TEXT:          c->texts++;  break;
    }
    if (ev->depth > c->max_depth)
        c->max_depth = ev->depth;
    return DUI_OK;
}

static void
test_events(void)
{
    static const char xml[] =
        "<!-- generated -->\n"
        "<duixml>\n"
        "  <stylesheets>\n"
        "    <style resid='main'>\n"
        "      <if id=\"atom(Sample)\"><element background=\"argb(255, 1, 2, 3)\" padding='rect(1rp,2rp,3rp,4rp)'/></if>\n"
        "      <Button foreground=\"windowtext\" font=\"gtf(TEXTSTYLE, 4, 0)\">a &amp; b</Button>\n"
        "    </style>\n"
        "  </stylesheets>\n"
        "</duixml>\n";
    counter c = { 0 };
    dui_xml_stats stats;
    dui_status st = dui_xml_parse((const uint8_t *)xml, sizeof(xml) - 1, count_events, &c, &stats, NULL);

    if (st != DUI_OK || c.starts != 6 || c.ends != 6 || c.attrs != 6 || c.texts != 1 ||
        c.max_depth != 5 || stats.elements != 6 || stats.attributes != 6 || stats.comments != 1)
    {
        fprintf(stderr, "FAIL events: st=%s starts=%u ends=%u attrs=%u texts=%u depth=%u\n",
                dui_status_name(st), c.starts, c.ends, c.attrs, c.texts, c.max_depth);
        failures++;
    }
}

static void
test_names(void)
{
    static const uint8_t data[] = "StyleSheets";
    dui_span s = { 0, 11 };

    if (!dui_name_equals(data, s, "stylesheets") || dui_name_equals(data, s, "stylesheet") ||
        dui_name_equals(data, s, "stylesheetsX"))
    {
        fprintf(stderr, "FAIL name folding\n");
        failures++;
    }
}

/* Builds "<a><a>...</a></a>" nested n deep. */
static char *
nested(unsigned n)
{
    char *p = malloc((size_t)n * 7 + 1), *q = p;
    unsigned i;

    if (!p)
        exit(2);
    for (i = 0; i < n; i++) { memcpy(q, "<a>", 3); q += 3; }
    for (i = 0; i < n; i++) { memcpy(q, "</a>", 4); q += 4; }
    *q = '\0';
    return p;
}

static void
test_limits(void)
{
    char *p;
    char buf[8192];
    size_t i, n;

    p = nested(DUI_XML_MAX_DEPTH);
    expect("depth at the limit", p, DUI_OK);
    free(p);
    p = nested(DUI_XML_MAX_DEPTH + 1);
    expect("depth over the limit (R-DUI-8, 9.5)", p, DUI_E_DEPTH);
    free(p);

    /* name of 64 characters accepted, 65 rejected */
    n = (size_t)snprintf(buf, sizeof(buf), "<");
    for (i = 0; i < DUI_XML_MAX_NAME_LEN; i++) buf[n++] = 'n';
    memcpy(buf + n, "/>", 3);
    expect("name at the limit", buf, DUI_OK);
    n = 1;
    for (i = 0; i <= DUI_XML_MAX_NAME_LEN; i++) buf[n++] = 'n';
    memcpy(buf + n, "/>", 3);
    expect("name over the limit (9.5)", buf, DUI_E_TOO_LARGE);

    /* value of 4096 bytes accepted, 4097 rejected */
    {
        size_t len = DUI_XML_MAX_VALUE_LEN + 1;
        char *v = malloc(len + 16);
        if (!v)
            exit(2);
        memcpy(v, "<a b='", 6);
        memset(v + 6, 'x', DUI_XML_MAX_VALUE_LEN);
        memcpy(v + 6 + DUI_XML_MAX_VALUE_LEN, "'/>", 4);
        expect("value at the limit", v, DUI_OK);
        memset(v + 6, 'x', len);
        memcpy(v + 6 + len, "'/>", 4);
        expect("value over the limit (9.5)", v, DUI_E_TOO_LARGE);
        free(v);
    }

    /* 64 attributes accepted, 65 rejected */
    n = (size_t)snprintf(buf, sizeof(buf), "<a");
    for (i = 0; i < DUI_XML_MAX_ATTRS_PER_ELEM; i++)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, " a%zu='1'", i);
    memcpy(buf + n, "/>", 3);
    expect("attributes at the limit", buf, DUI_OK);
    n = (size_t)snprintf(buf, sizeof(buf), "<a");
    for (i = 0; i <= DUI_XML_MAX_ATTRS_PER_ELEM; i++)
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, " a%zu='1'", i);
    memcpy(buf + n, "/>", 3);
    expect("attributes over the limit (9.5)", buf, DUI_E_TOO_LARGE);

    /* elements over the limit */
    {
        size_t count = DUI_XML_MAX_ELEMENTS; /* plus the root: one over */
        size_t len = 3 + count * 4 + 4 + 1;
        char *e = malloc(len), *q = e;
        if (!e)
            exit(2);
        memcpy(q, "<r>", 3); q += 3;
        for (i = 0; i < count; i++) { memcpy(q, "<a/>", 4); q += 4; }
        memcpy(q, "</r>", 5);
        expect("elements over the limit (9.5)", e, DUI_E_TOO_LARGE);
        free(e);
    }

    /* size over the limit */
    {
        size_t len = DUI_XML_MAX_SIZE + 1;
        uint8_t *big = malloc(len);
        if (!big)
            exit(2);
        memset(big, ' ', len);
        expect_bytes("size over the limit (9.5)", big, len, DUI_E_TOO_LARGE);
        free(big);
    }
}

int
main(void)
{
    static const uint8_t with_nul[] = { '<', 'a', '/', '>', 0 };
    static const uint8_t bad_utf8[] = { '<', 'a', ' ', 'b', '=', '\'', 0xC3, '\'', '/', '>' };
    static const uint8_t overlong[] = { '<', 'a', ' ', 'b', '=', '\'', 0xC0, 0x80, '\'', '/', '>' };
    static const uint8_t surrogate[] = { '<', 'a', ' ', 'b', '=', '\'', 0xED, 0xA0, 0x80, '\'', '/', '>' };
    static const uint8_t good_utf8[] = { '<', 'a', ' ', 'b', '=', '\'', 0xC3, 0xA9, '\'', '/', '>' };

    test_events();
    test_names();
    test_limits();

    /* accepted */
    expect("empty element", "<a/>", DUI_OK);
    expect("white space and comments around the root", " <!-- x --> <a></a> <!-- y -->\n", DUI_OK);
    expect("predefined entities", "<a b='&lt;&gt;&amp;&quot;&apos;'>&amp;</a>", DUI_OK);
    expect("close tag is case-insensitive", "<Element></element>", DUI_OK);
    expect_bytes("valid UTF-8", good_utf8, sizeof(good_utf8), DUI_OK);

    /* R-DUI-5 / arguments */
    expect_bytes("empty input", (const uint8_t *)"", 0, DUI_E_ARG);
    /* R-DUI-6 */
    expect_bytes("NUL byte", with_nul, sizeof(with_nul), DUI_E_FORBIDDEN);
    expect_bytes("truncated UTF-8", bad_utf8, sizeof(bad_utf8), DUI_E_SYNTAX);
    expect_bytes("overlong UTF-8", overlong, sizeof(overlong), DUI_E_SYNTAX);
    expect_bytes("UTF-8 surrogate", surrogate, sizeof(surrogate), DUI_E_SYNTAX);
    /* R-DUI-7 */
    expect("XML declaration", "<?xml version='1.0'?><a/>", DUI_E_FORBIDDEN);
    expect("processing instruction", "<a><?pi x?></a>", DUI_E_FORBIDDEN);
    expect("DOCTYPE", "<!DOCTYPE a><a/>", DUI_E_FORBIDDEN);
    expect("ENTITY", "<a><!ENTITY x 'y'></a>", DUI_E_FORBIDDEN);
    expect("CDATA", "<a><![CDATA[x]]></a>", DUI_E_FORBIDDEN);
    expect("character reference", "<a b='&#65;'/>", DUI_E_FORBIDDEN);
    expect("hex character reference", "<a>&#x41;</a>", DUI_E_FORBIDDEN);
    expect("other entity", "<a b='&nbsp;'/>", DUI_E_FORBIDDEN);
    expect("bare ampersand", "<a b='a & b'/>", DUI_E_FORBIDDEN);
    /* R-DUI-8 */
    expect("mismatched close tag", "<a></b>", DUI_E_SYNTAX);
    expect("unclosed root", "<a><b/>", DUI_E_SYNTAX);
    expect("close without open", "</a>", DUI_E_SYNTAX);
    expect("duplicate attribute", "<a b='1' b='2'/>", DUI_E_SYNTAX);
    expect("duplicate attribute, other case", "<a b='1' B='2'/>", DUI_E_SYNTAX);
    expect("second root", "<a/><b/>", DUI_E_SYNTAX);
    expect("text after the root", "<a/>x", DUI_E_SYNTAX);
    expect("text before the root", "x<a/>", DUI_E_SYNTAX);
    expect("unterminated comment", "<a/><!-- x", DUI_E_SYNTAX);
    expect("double hyphen in a comment", "<a/><!-- a -- b -->", DUI_E_SYNTAX);
    expect("unterminated value", "<a b='1/>", DUI_E_SYNTAX);
    expect("unquoted value", "<a b=1/>", DUI_E_SYNTAX);
    expect("less-than in a value", "<a b='<'/>", DUI_E_SYNTAX);
    expect("attributes not separated", "<a b='1'c='2'/>", DUI_E_SYNTAX);
    expect("unterminated start tag", "<a b='1'", DUI_E_SYNTAX);
    expect("only a comment", "<!-- x -->", DUI_E_SYNTAX);
    /* R-DUI-9 */
    expect("name starting with a digit", "<1a/>", DUI_E_SYNTAX);
    expect("name with a colon", "<a:b/>", DUI_E_SYNTAX);
    expect("non-ASCII name", "<\xc3\xa9/>", DUI_E_SYNTAX);

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
