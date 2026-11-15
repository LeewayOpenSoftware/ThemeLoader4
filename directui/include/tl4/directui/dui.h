/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Common types of the DirectUI component
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#ifndef TL4_DIRECTUI_DUI_H
#define TL4_DIRECTUI_DUI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. One code per reason (R-GEN-5); a failed call returns no partial result. */
typedef enum dui_status
{
    DUI_OK = 0,
    DUI_E_ARG,          /* a caller argument is invalid */
    DUI_E_TOO_LARGE,    /* a size or count limit is exceeded */
    DUI_E_SYNTAX,       /* the input is not in the accepted dialect */
    DUI_E_FORBIDDEN,    /* the input uses a construct the dialect refuses */
    DUI_E_DEPTH,        /* a nesting limit is exceeded */
    DUI_E_NO_MEMORY     /* an allocation after validation failed */
} dui_status;

/* A span of the caller's input: [offset, offset + length). Never a pointer. */
typedef struct dui_span
{
    uint32_t offset;
    uint32_t length;
} dui_span;

const char *dui_version(void);
const char *dui_status_name(dui_status status);

#ifdef __cplusplus
}
#endif

#endif /* TL4_DIRECTUI_DUI_H */
