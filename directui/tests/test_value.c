/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests of the value-expression parser; every fixture is written here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <string.h>
#include <tl4/directui/dui_value.h>

static int failures;
static dui_value v;

static dui_status
parse(const char *text)
{
    dui_span s = { 0, (uint32_t)strlen(text) };
    return dui_value_parse((const uint8_t *)text, strlen(text), s, &v);
}

static void
expect(const char *text, dui_status expected)
{
    dui_status st = parse(text);

    if (st != expected)
    {
        fprintf(stderr, "FAIL \"%s\": got %s, expected %s\n", text, dui_status_name(st), dui_status_name(expected));
        failures++;
    }
}

static void
expect_root(const char *text, dui_value_kind kind, int32_t number, uint16_t count)
{
    dui_status st = parse(text);

    if (st != DUI_OK || v.nodes[0].kind != kind || v.nodes[0].number != number || v.count != count)
    {
        fprintf(stderr, "FAIL shape \"%s\": st=%s kind=%u number=%d count=%u\n", text, dui_status_name(st),
                (unsigned)v.nodes[0].kind, (int)v.nodes[0].number, (unsigned)v.count);
        failures++;
    }
}

static void
test_tree(void)
{
    const char *text = "themeable(gtc(CPL::CommandModule, 1, 2, 3801), windowtext)";
    const dui_value_node *n;

    if (parse(text) != DUI_OK || v.count != 7)
    {
        fprintf(stderr, "FAIL tree parse (count %u)\n", (unsigned)v.count);
        failures++;
        return;
    }
    n = &v.nodes[0];
    if (n->kind != DUI_V_CALL || n->function != DUI_FN_THEMEABLE || n->child_count != 2 ||
        v.nodes[n->first_child].function != DUI_FN_GTC || v.nodes[v.nodes[n->first_child].next].kind != DUI_V_NAME ||
        v.nodes[v.nodes[n->first_child].first_child].text.length != 18 || v.nodes[6].parent != 0)
    {
        fprintf(stderr, "FAIL tree shape\n");
        failures++;
    }
}

int
main(void)
{
    char buf[1024];
    size_t i, n;

    test_tree();

    /* leaves */
    expect_root("42", DUI_V_INT, 42, 1);
    expect_root("-7", DUI_V_INT, -7, 1);
    expect_root("0x1F", DUI_V_INT, 31, 1);
    expect_root("12rp", DUI_V_LENGTH, 12, 1);
    expect_root("windowtext", DUI_V_NAME, 0, 1);
    expect_root("'ab'", DUI_V_STRING, 2, 1);
    expect_root("middle | left | endellipsis", DUI_V_FLAGS, 3, 1);
    expect_root("0x1 | 0x10", DUI_V_FLAGS, 2, 1);
    expect_root("caption;120%", DUI_V_FONTSPEC, 120, 1);

    /* calls seen in style sheets */
    expect("argb(255, 0, 128, 255)", DUI_OK);
    expect("rect(1rp, 2rp, 0, 4rp)", DUI_OK);
    expect("gtf(TEXTSTYLE, 4, 0)", DUI_OK);
    expect("dtb(PreviewPane, 1, 1)", DUI_OK);
    expect("themeable(dtb(Tab, 1, 1), dfc(4, 0x10 | 0x400))", DUI_OK);
    expect("gradient(argb(255,1,2,3), argb(255,4,5,6), argb(255,7,8,9), 1)", DUI_OK);
    expect("size(10rp, 20rp)", DUI_OK);
    expect("borderlayout()", DUI_OK);
    expect("flowlayout(1, 2, 3, 4)", DUI_OK);
    expect("resstr(0x2001, library(shell32.dll))", DUI_OK);
    expect("icon(5, sysmetric(49), sysmetric(50), library(shell32.dll))", DUI_OK);
    expect("unknownfunction('x', 1)", DUI_OK);
    if (parse("unknownfunction(a)") != DUI_OK || v.unknown_calls != 1)
    {
        fprintf(stderr, "FAIL unknown call not counted\n");
        failures++;
    }

    /* syntax */
    expect("", DUI_E_SYNTAX);
    expect("f(", DUI_E_SYNTAX);
    expect("f(a,)", DUI_E_SYNTAX);
    expect("f(a))", DUI_E_SYNTAX);
    expect("a b", DUI_E_SYNTAX);
    expect("1.5", DUI_E_SYNTAX);
    expect("10px", DUI_E_SYNTAX);
    expect("'open", DUI_E_SYNTAX);
    expect("{guid}", DUI_E_SYNTAX);
    expect("a | ", DUI_E_SYNTAX);
    expect("caption;12", DUI_E_SYNTAX);

    /* R-DUI-11: known functions are checked */
    expect("argb(1, 2, 3)", DUI_E_SYNTAX);
    expect("rect(1, 2, 3, x)", DUI_E_SYNTAX);
    expect("gtc(1, 2, 3, 4)", DUI_E_SYNTAX);
    expect("atom(1)", DUI_E_SYNTAX);
    expect("library(shell32.dll)", DUI_E_SYNTAX); /* only inside a resource reference */
    /* R-DUI-12 */
    expect("1000000", DUI_OK);
    expect("1000001", DUI_E_TOO_LARGE);
    expect("-1000001", DUI_E_TOO_LARGE);
    expect("0x100000", DUI_E_TOO_LARGE);
    expect("99999999999999999999", DUI_E_TOO_LARGE);
    expect("argb(256, 0, 0, 0)", DUI_E_TOO_LARGE);
    expect("argb(-1, 0, 0, 0)", DUI_E_TOO_LARGE);
    /* R-DUI-13 */
    expect("resstr(1, library(shell32::dll))", DUI_E_FORBIDDEN);
    expect("resstr(1, library(abcdefghijklmnopqrstuvwxyz0123456789.dll))", DUI_E_FORBIDDEN);
    expect("resstr(1, library('c:/x.dll'))", DUI_E_SYNTAX);
    /* R-DUI-14 */
    expect("resstr(0)", DUI_E_TOO_LARGE);
    expect("resstr(65536)", DUI_E_TOO_LARGE);
    expect("icon(5, 2000, 16, library(a.dll))", DUI_E_TOO_LARGE);
    expect("resstr(1, atom(x))", DUI_E_SYNTAX);
    /* R-DUI-15 */
    expect("gtc(abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefgh, 1, 1, 1)", DUI_E_TOO_LARGE);
    expect("gtc(Button, 256, 1, 1)", DUI_E_TOO_LARGE);
    expect("gtc(Button, 1, 1, 0)", DUI_E_TOO_LARGE);
    expect("sysmetric(0)", DUI_E_FORBIDDEN);
    expect("sysmetric(49)", DUI_OK);

    /* 9.5 limits: nesting 8, 16 arguments, 256 nodes */
    n = 0;
    for (i = 0; i < DUI_VALUE_MAX_NESTING; i++) n += (size_t)snprintf(buf + n, sizeof(buf) - n, "f(");
    n += (size_t)snprintf(buf + n, sizeof(buf) - n, "1");
    for (i = 0; i < DUI_VALUE_MAX_NESTING; i++) buf[n++] = ')';
    buf[n] = '\0';
    expect(buf, DUI_OK);
    n = 0;
    for (i = 0; i <= DUI_VALUE_MAX_NESTING; i++) n += (size_t)snprintf(buf + n, sizeof(buf) - n, "f(");
    n += (size_t)snprintf(buf + n, sizeof(buf) - n, "1");
    for (i = 0; i <= DUI_VALUE_MAX_NESTING; i++) buf[n++] = ')';
    buf[n] = '\0';
    expect(buf, DUI_E_DEPTH);
    n = (size_t)snprintf(buf, sizeof(buf), "f(1");
    for (i = 1; i < DUI_VALUE_MAX_ARGS; i++) n += (size_t)snprintf(buf + n, sizeof(buf) - n, ",1");
    snprintf(buf + n, sizeof(buf) - n, ")");
    expect(buf, DUI_OK);
    n = (size_t)snprintf(buf, sizeof(buf), "f(1");
    for (i = 1; i <= DUI_VALUE_MAX_ARGS; i++) n += (size_t)snprintf(buf + n, sizeof(buf) - n, ",1");
    snprintf(buf + n, sizeof(buf) - n, ")");
    expect(buf, DUI_E_TOO_LARGE);
    {
        /* 15 calls of 16 arguments: 1 + 15 * 17 = 256 nodes; one more argument is over */
        static char big[8192];
        size_t k, j;
        n = (size_t)snprintf(big, sizeof(big), "g(");
        for (k = 0; k < 15; k++)
        {
            n += (size_t)snprintf(big + n, sizeof(big) - n, "%sf(1", k ? "," : "");
            for (j = 1; j < 16; j++) n += (size_t)snprintf(big + n, sizeof(big) - n, ",1");
            big[n++] = ')';
        }
        snprintf(big + n, sizeof(big) - n, ")");
        expect(big, DUI_OK);
        big[n] = '\0';
        snprintf(big + n, sizeof(big) - n, ",1)");
        expect(big, DUI_E_TOO_LARGE);
    }

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
