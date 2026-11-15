/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Tests of the theme style-sheet model; every fixture is written here
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <string.h>
#include <tl4/directui/dui_theme.h>

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
        "  <Button><if pressed='true'><Button padding='rect(1rp, 2rp, 3rp, 4rp)'/></if></Button>"
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
    if (s.stats.sheets != 2 || s.stats.scopes != 2 || s.stats.rules != 5 || s.stats.declarations != 5 ||
        s.stats.conditions != 4 || s.stats.dropped != 5 || s.stats.dropped_by_reason[DUI_DROP_CONTENT] != 2 ||
        s.stats.dropped_by_reason[DUI_DROP_IDENTITY] != 2 || s.stats.dropped_by_reason[DUI_DROP_BEHAVIOUR] != 1 ||
        s.sheets[0].rule_count != 4 || s.sheets[1].rule_count != 1)
    {
        fprintf(stderr, "FAIL model counts: sheets %u scopes %u rules %u decls %u conds %u dropped %u\n",
                s.stats.sheets, s.stats.scopes, s.stats.rules, s.stats.declarations, s.stats.conditions,
                s.stats.dropped);
        failures++;
    }
    /* rule 0 is inside scope 0; rule 3 (inner Button) is inside scope 1, itself inside rule 2 */
    if (s.rules[0].scope != 0 || s.rules[1].scope != DUI_SHEET_NONE || s.rules[1].condition_count != 1 ||
        s.rules[3].scope != 1 || s.scopes[1].parent_rule != 2 || s.scopes[0].condition_count != 2 ||
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
    if (dui_resolve_color(&s, &s.declarations[s.rules[3].first_declaration], &q, &argb) != DUI_E_ARG)
    {
        fprintf(stderr, "FAIL rect resolved as a colour\n");
        failures++;
    }
    dui_sheets_free(&s);
}

int
main(void)
{
    test_model();

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
    expect("attribute on duixml", "<duixml x='1'><stylesheets><style resid='s'/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    expect("style without resid", "<duixml><stylesheets><style/></stylesheets></duixml>", DUI_E_SYNTAX);
    expect("style with another attribute", "<duixml><stylesheets><style resid='s' x='1'/></stylesheets></duixml>", DUI_E_FORBIDDEN);
    /* R-DUI-3: dropped, not failed */
    expect("dropped properties", SHEET("<Button content='resstr(1)' visible='false' sheet='x'/>"), DUI_OK);
    /* a kept value that is not well formed is a reject (R-GEN-5) */
    expect("bad visual value", SHEET("<Button background='argb(300,0,0,0)'/>"), DUI_E_TOO_LARGE);
    expect("bad condition value", SHEET("<if id='atom('><Button/></if>"), DUI_E_SYNTAX);
    /* R-DUI-10: unknown selector tags and conditions are kept */
    expect("unknown selector and condition", SHEET("<SomeNewControl><if futureflag='true'><SomeNewControl width='3rp'/></if></SomeNewControl>"), DUI_OK);

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    return 0;
}
