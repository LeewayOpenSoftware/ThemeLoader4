/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests of the consumer layout entry point; every fixture is written here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <string.h>
#include <tl4/directui/dui_layout.h>
#include <tl4/directui/dui_sheet.h>
#include <tl4/directui/dui_xml.h>

static int failures;

static void
check(int condition, const char *what)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL %s\n", what);
        failures++;
    }
}

int
main(void)
{
    static const char xml[] =
        "<duixml>"
        "<element id='atom(root)' layout='borderlayout()'>"
        "  <Button id='atom(ok)' content='resstr(12, library(x.dll))'/>"
        "  <macro id='m'><element/></macro>"
        "  <bind property='x' to='y'/>"
        "  <Sizer>text</Sizer>"
        "</element>"
        "<stylesheets><style resid='s'><Button background='window'/></style></stylesheets>"
        "</duixml>";
    dui_layout l;
    dui_sheets s;
    dui_status st;

    st = dui_layout_load((const uint8_t *)xml, sizeof(xml) - 1, &l, NULL);
    check(st == DUI_OK, "layout loads");
    if (st == DUI_OK)
    {
        const dui_element *root = &l.elements[0], *el = &l.elements[root->first_child];
        check(l.element_count == 10 && l.attribute_count == 9, "element and attribute counts");
        check(l.macros == 1 && l.binds == 1 && l.text_runs == 1, "macro, bind, text counted");
        check(root->parent == DUI_LAYOUT_NONE && dui_name_equals(l.input, root->tag, "duixml"), "root");
        check(dui_name_equals(l.input, el->tag, "element") && el->attribute_count == 2, "first child");
        check(dui_name_equals(l.input, l.attributes[el->first_attribute + 1].name, "layout"), "attribute order");
        check(dui_name_equals(l.input, l.elements[el->next].tag, "stylesheets"), "sibling link");
        check(l.elements[l.elements[el->first_child].next].parent == root->first_child, "parent link");
    }
    dui_layout_free(&l);

    /* R-DUI-4: the same bytes are refused by the theme entry point */
    st = dui_sheets_load((const uint8_t *)xml, sizeof(xml) - 1, &s, NULL);
    check(st == DUI_E_FORBIDDEN, "layout refused as theme input");
    dui_sheets_free(&s);

    /* the tokenizer's rules still apply */
    st = dui_layout_load((const uint8_t *)"<a><b></a>", 10, &l, NULL);
    check(st == DUI_E_SYNTAX && l.elements == NULL, "malformed layout refused without a partial result");
    dui_layout_free(&l);

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
