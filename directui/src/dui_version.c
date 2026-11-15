/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Version and status names
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui.h>

const char *
dui_version(void)
{
    return "tl4-directui 0.1.0";
}

const char *
dui_status_name(dui_status status)
{
    switch (status)
    {
        case DUI_OK:          return "ok";
        case DUI_E_ARG:       return "invalid argument";
        case DUI_E_TOO_LARGE: return "limit exceeded";
        case DUI_E_SYNTAX:    return "syntax error";
        case DUI_E_FORBIDDEN: return "forbidden construct";
        case DUI_E_DEPTH:     return "nesting too deep";
        case DUI_E_NO_MEMORY: return "out of memory";
    }
    return "unknown status";
}
