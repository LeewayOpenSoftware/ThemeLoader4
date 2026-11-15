/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Abstract theme-query interface and value resolution
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * DirectUI asks for theme data through this interface only. The integration (milestone I1)
 * binds it to uxtheme; tests bind it to a fake. directui/ never includes or links loader/.
 */

#ifndef TL4_DIRECTUI_DUI_THEME_H
#define TL4_DIRECTUI_DUI_THEME_H

#include <tl4/directui/dui_sheet.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dui_theme_query
{
    void *context;
    /* Nonzero when a visual style is active (themeable() then prefers its first argument). */
    int (*is_active)(void *context);
    /* GetThemeColor equivalent. class_name is not NUL-terminated. Returns nonzero on success. */
    int (*get_color)(void *context, const char *class_name, size_t class_length,
                     int part, int state, int property, uint32_t *argb);
    /* A named system colour (windowtext, window, buttonface, ...). Returns nonzero on success. */
    int (*get_named_color)(void *context, const char *name, size_t length, uint32_t *argb);
} dui_theme_query;

/* Resolve a colour declaration to 0xAARRGGBB: argb(), rgb(), gtc(), a named colour, and
 * themeable(themed, fallback). No recursion; values from the query are used as returned
 * (a colour has no range to clamp). DUI_E_ARG if the value is not a colour. */
dui_status dui_resolve_color(const dui_sheets *sheets, const dui_declaration *declaration,
                             const dui_theme_query *query, uint32_t *argb);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_THEME_H */
