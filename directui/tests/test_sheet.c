/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests of the theme style-sheet model; every fixture is written here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef DUI_TESTING
#error "this test needs the DUI_TESTING build of the core (tl4_directui_core_testing)"
#endif
#include <tl4/directui/dui_theme.h>

/* Test hook in dui_sheet.c (R-GEN-11): called between the two passes. */
extern void (*dui_sheets_test_between_passes)(const uint8_t *data, size_t size);

static int failures;

static dui_status
load(const char *xml, dui_sheets *s)
{
    return dui_sheets_load((const uint8_t *)xml, strlen(xml), s, NULL);
}

static void
expect(const char *what, const char *xml, dui_status expected)
{
    dui_sheets s;
    dui_status st = load(xml, &s);

    if (st != expected)
    {
        fprintf(stderr, "FAIL %s: got %s, expected %s\n", what, dui_status_name(st), dui_status_name(expected));
        failures++;
    }
    if (st != DUI_OK && (s.input || s.rules || s.nodes))
    {
        fprintf(stderr, "FAIL %s: partial result after failure\n", what);
        failures++;
    }
    dui_sheets_free(&s);
}

#define SHEET(body) "<duixml><stylesheets><style resid='s'>" body "</style></stylesheets></duixml>"

/* fake theme query */
static int active;
static int q_active(void *c) { (void)c; return active; }
static int
q_color(void *c, const char *cls, size_t len, int part, int state, int prop, uint32_t *argb)
{
    (void)c;
    if (len == 6 && memcmp(cls, "Button", 6) == 0 && part == 1 && state == 2 && prop == 3803)
    {
        *argb = 0xFF112233u;
        return 1;
    }
    return 0;
}
static int
q_named(void *c, const char *name, size_t len, uint32_t *argb)
{
    (void)c;
    if (len == 10 && memcmp(name, "windowtext", 10) == 0)
    {
        *argb = 0xFF000000u;
        return 1;
    }
    return 0;
}

static void
test_model(void)
{
    static const char xml[] =
        "<duixml><stylesheets>"
        "<style resid='first'>"
        "  <if id='atom(Pane)' class='header'>"
        "    <Element background='themeable(gtc(Button, 1, 2, 3803), windowtext)' content='x' id='atom(y)'/>"
        "  </if>"
        "  <Button keyfocused='true' foreground='argb(255, 1, 2, 3)' cursor='hand' tooltip='t' layout='filllayout()'/>"
        "  <if pressed='true'><Button padding='rect(1rp, 2rp, 3rp, 4rp)'/></if>"
        "</style>"
        "<style resid='second'><element font='caption;120%' contentalign='middle | left'/></style>"
        "</stylesheets></duixml>";
    const dui_theme_query q = { NULL, q_active, q_color, q_named };
    dui_sheets s;
    uint32_t argb = 0;
    dui_status st = load(xml, &s);

    if (st != DUI_OK)
    {
        fprintf(stderr, "FAIL model load: %s\n", dui_status_name(st));
        failures++;
        return;
    }
    if (s.stats.sheets != 2 || s.stats.scopes != 2 || s.stats.rules != 4 || s.stats.declarations != 5 ||
        s.stats.conditions != 4 || s.stats.dropped != 5 || s.stats.dropped_by_reason[DUI_DROP_CONTENT] != 2 ||
        s.stats.dropped_by_reason[DUI_DROP_IDENTITY] != 2 || s.stats.dropped_by_reason[DUI_DROP_BEHAVIOUR] != 1 ||
        s.sheets[0].rule_count != 3 || s.sheets[1].rule_count != 1)
    {
        fprintf(stderr, "FAIL model counts: sheets %u scopes %u rules %u decls %u conds %u dropped %u\n",
                s.stats.sheets, s.stats.scopes, s.stats.rules, s.stats.declarations, s.stats.conditions,
                s.stats.dropped);
        failures++;
    }
    /* rule 0 is inside scope 0; rule 2 is inside scope 1, which is at the style level */
    if (s.rules[0].scope != 0 || s.rules[1].scope != DUI_SHEET_NONE || s.rules[1].condition_count != 1 ||
        s.rules[2].scope != 1 || s.scopes[1].parent_scope != DUI_SHEET_NONE || s.scopes[0].condition_count != 2 ||
        s.conditions[s.scopes[0].first_condition].kind != DUI_COND_ID)
    {
        fprintf(stderr, "FAIL model structure\n");
        failures++;
    }

    /* colour resolution: themeable with and without an active style */
    active = 1;
    if (dui_resolve_color(&s, &s.declarations[s.rules[0].first_declaration], &q, &argb) != DUI_OK || argb != 0xFF112233u)
    {
        fprintf(stderr, "FAIL themed colour %08x\n", argb);
        failures++;
    }
    active = 0;
    if (dui_resolve_color(&s, &s.declarations[s.rules[0].first_declaration], &q, &argb) != DUI_OK || argb != 0xFF000000u)
    {
        fprintf(stderr, "FAIL fallback colour %08x\n", argb);
        failures++;
    }
    if (dui_resolve_color(&s, &s.declarations[s.rules[1].first_declaration], &q, &argb) != DUI_OK || argb != 0xFF010203u)
    {
        fprintf(stderr, "FAIL argb colour %08x\n", argb);
        failures++;
    }
    if (dui_resolve_color(&s, &s.declarations[s.rules[2].first_declaration], &q, &argb) != DUI_E_ARG)
    {
        fprintf(stderr, "FAIL rect resolved as a colour\n");
        failures++;
    }
    dui_sheets_free(&s);
}

/* Repeats `unit` n times inside one sheet. */
static char *
repeat_sheet(const char *unit, size_t n)
{
    static const char head[] = "<duixml><stylesheets><style resid='s'>";
    static const char tail[] = "</style></stylesheets></duixml>";
    size_t ulen = strlen(unit), i;
    char *p = malloc(sizeof(head) + sizeof(tail) + ulen * n), *q;

    if (!p)
        exit(2);
    q = p;
    memcpy(q, head, sizeof(head) - 1); q += sizeof(head) - 1;
    for (i = 0; i < n; i++) { memcpy(q, unit, ulen); q += ulen; }
    memcpy(q, tail, sizeof(tail));
    return p;
}

static void
test_limits(void)
{
    static const char eighteen[] =
        "<a background='1' foreground='1' font='1' fontstyle='1' fontsize='1' fontweight='1' fontface='1' "
        "padding='1' margin='1' pressedpaddingshift='1' contentalign='1' height='1' width='1' minsize='1' "
        "borderthickness='1' bordercolor='1' borderstyle='1' transparent='1'/>";
    char *p;

    p = repeat_sheet("<if id='atom(x)'/>", DUI_SHEET_MAX_SCOPES);
    expect("scopes at the limit", p, DUI_OK);
    free(p);
    p = repeat_sheet("<if id='atom(x)'/>", DUI_SHEET_MAX_SCOPES + 1);
    expect("scopes over the limit", p, DUI_E_TOO_LARGE);
    free(p);
    p = repeat_sheet(eighteen, DUI_SHEET_MAX_DECLARATIONS / 18 + 1);
    expect("declarations over the limit", p, DUI_E_TOO_LARGE);
    free(p);
    p = repeat_sheet("<a padding='rect(1,2,3,4)'/>", DUI_SHEET_MAX_VALUE_NODES / 5 + 1);
    expect("value nodes over the limit", p, DUI_E_TOO_LARGE);
    free(p);
}

/* R-GEN-11: the buffer changes between the passes. Pass 1 counts no rule (a comment); the hook
 * turns the comment into rules of the same length. Pass 2 must refuse, not write past the arrays
 * (AddressSanitizer would catch a write). */
static char mutable_input[256];
static void
mutate(const uint8_t *data, size_t size)
{
    char *p = strstr(mutable_input, "<!--");
    (void)data;
    (void)size;
    if (p)
        memcpy(p, "<a/><a/><a/><a/>", 16);
}

static void
test_changing_input(void)
{
    dui_sheets s;
    dui_status st;

    strcpy(mutable_input, "<duixml><stylesheets><style resid='s'><!--xxxxxxxx--></style></stylesheets></duixml>");
    dui_sheets_test_between_passes = mutate;
    st = dui_sheets_load((const uint8_t *)mutable_input, strlen(mutable_input), &s, NULL);
    dui_sheets_test_between_passes = NULL;
    if (st == DUI_OK || s.rules)
    {
        fprintf(stderr, "FAIL changing input accepted (%s)\n", dui_status_name(st));
        failures++;
    }
    dui_sheets_free(&s);
}

int
main(void)
{
    test_model();
    test_limits();
    test_changing_input();

    expect("minimal sheet", SHEET("<Button background='argb(255,0,0,0)'/>"), DUI_OK);
    expect("empty style", SHEET(""), DUI_OK);
    /* R-DUI-2: style sheets only */
    expect("no style sheet", "<duixml><stylesheets></stylesheets></duixml>", DUI_E_SYNTAX);
    expect("wrong root", "<element><stylesheets/></element>", DUI_E_FORBIDDEN);
    expect("element tree beside the sheets", "<duixml><element id='atom(x)'/></duixml>", DUI_E_FORBIDDEN);
    expect("element under stylesheets", "<duixml><stylesheets><element/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    expect("macro", SHEET("<macro/>"), DUI_E_FORBIDDEN);
    expect("bind", SHEET("<if id='atom(x)'><bind/></if>"), DUI_E_FORBIDDEN);
    expect("text inside a selector", SHEET("<Button>label</Button>"), DUI_E_FORBIDDEN);
    expect("text at the root", "<duixml>x<stylesheets><style resid='s'/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    expect("stray text between rules", SHEET("<Button/> \\ <if id='atom(x)'> \\ <Button/></if>"), DUI_OK);
    expect("nested selector", SHEET("<Button><Element/></Button>"), DUI_E_FORBIDDEN);
    expect("selector in an if in a selector", SHEET("<Button><if pressed='true'><Button/></if></Button>"), DUI_E_FORBIDDEN);
    /* resid is a name (review SHOULD 2) */
    expect("resid with a space", "<duixml><stylesheets><style resid='a b'/></stylesheets></duixml>", DUI_E_SYNTAX);
    expect("resid starting with a digit", "<duixml><stylesheets><style resid='1a'/></stylesheets></duixml>", DUI_E_SYNTAX);
    expect("resid with dot and dash", "<duixml><stylesheets><style resid='a.b-c_1'/></stylesheets></duixml>", DUI_OK);
    /* R-DUI-3a */
    expect("negative height", SHEET("<Button height='-1rp'/>"), DUI_E_FORBIDDEN);
    expect("negative padding", SHEET("<Button padding='rect(0, -2rp, 0, 0)'/>"), DUI_E_FORBIDDEN);
    expect("negative fontsize", SHEET("<Button fontsize='-3'/>"), DUI_E_FORBIDDEN);
    expect("negative pressedpaddingshift", SHEET("<Button pressedpaddingshift='rect(-1rp, -1rp, 0, 0)'/>"), DUI_OK);
    expect("negative colour component stays a range error", SHEET("<Button background='argb(255, 0, 0, 0)' height='0'/>"), DUI_OK);
    expect("attribute on duixml", "<duixml x='1'><stylesheets><style resid='s'/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    expect("style without resid", "<duixml><stylesheets><style/></stylesheets></duixml>", DUI_E_SYNTAX);
    expect("style with another attribute", "<duixml><stylesheets><style resid='s' x='1'/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    /* R-DUI-3: dropped, not failed */
    expect("dropped properties", SHEET("<Button content='resstr(1)' visible='false' sheet='x'/>"), DUI_OK);
    /* a kept value that is not well formed is a reject (R-GEN-5) */
    expect("bad visual value", SHEET("<Button background='argb(300,0,0,0)'/>"), DUI_E_TOO_LARGE);
    expect("bad condition value", SHEET("<if id='atom('><Button/></if>"), DUI_E_SYNTAX);
    /* R-DUI-10: unknown selector tags and conditions are kept */
    expect("unknown selector and condition", SHEET("<SomeNewControl/><if futureflag='true'><SomeNewControl width='3rp' futurestate='1'/></if>"), DUI_OK);

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
