/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Build and link smoke test
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <stdio.h>
#include <string.h>
#include <tl4/directui/dui.h>

int
main(void)
{
    if (strncmp(dui_version(), "tl4-directui ", 13) != 0)
    {
        fprintf(stderr, "unexpected version %s\n", dui_version());
        return 1;
    }
    if (strcmp(dui_status_name(DUI_E_DEPTH), "nesting too deep") != 0)
    {
        fprintf(stderr, "status names out of date\n");
        return 1;
    }
    return 0;
}
