/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for input that changes between the two passes (R-GEN-11,
 *              S1-FUZZ-PLAN section 4)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The input is split into two documents A and B of the same length (byte 0 selects the loader).
 * The loader runs on a buffer holding A; the between-passes hook overwrites it with B. The only
 * acceptable outcomes are a clean reject, or a model equal to the one A or B gives on its own.
 * Built against tl4_directui_core_testing (DUI_TESTING): the shipped core has no hook.
 */

#ifndef DUI_TESTING
#error "fuzz_two_pass needs the DUI_TESTING build of the core"
#endif

#include "dui_fuzz_check.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

extern void (*dui_sheets_test_between_passes)(const uint8_t *data, size_t size);
extern void (*dui_layout_test_between_passes)(const uint8_t *data, size_t size);

static uint8_t *swap_buffer;
static const uint8_t *swap_source;
static size_t swap_size;

static void
swap_in_b(const uint8_t *data, size_t size)
{
    FUZZ_CHECK(data == swap_buffer && size == swap_size);
    memcpy(swap_buffer, swap_source, swap_size);
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    const uint8_t *a, *b;
    size_t half;
    int use_layout;

    if (size < 3)
        return 0;
    use_layout = data[0] & 1;
    half = (size - 1) / 2;
    a = data + 1;
    b = data + 1 + half;

    swap_buffer = fuzz_exact_copy(a, half);
    swap_source = b;
    swap_size = half;

    if (use_layout)
    {
        dui_layout changed, la, lb;
        dui_status rc, ra, rb;

        dui_layout_test_between_passes = swap_in_b;
        rc = dui_layout_load(swap_buffer, half, &changed, NULL);
        dui_layout_test_between_passes = NULL;
        if (rc != DUI_OK)
        {
            FUZZ_CHECK(layout_cleared(&changed));
        }
        else
        {
            ra = dui_layout_load(a, half, &la, NULL);
            rb = dui_layout_load(b, half, &lb, NULL);
            FUZZ_CHECK((ra == DUI_OK && layouts_eq(&changed, &la)) || (rb == DUI_OK && layouts_eq(&changed, &lb)));
            dui_layout_free(&la);
            dui_layout_free(&lb);
        }
        dui_layout_free(&changed);
    }
    else
    {
        dui_sheets changed, sa, sb;
        dui_status rc, ra, rb;

        dui_sheets_test_between_passes = swap_in_b;
        rc = dui_sheets_load(swap_buffer, half, &changed, NULL);
        dui_sheets_test_between_passes = NULL;
        if (rc != DUI_OK)
        {
            FUZZ_CHECK(sheets_cleared(&changed));
        }
        else
        {
            ra = dui_sheets_load(a, half, &sa, NULL);
            rb = dui_sheets_load(b, half, &sb, NULL);
            FUZZ_CHECK((ra == DUI_OK && sheets_eq(&changed, &sa)) || (rb == DUI_OK && sheets_eq(&changed, &sb)));
            dui_sheets_free(&sa);
            dui_sheets_free(&sb);
        }
        dui_sheets_free(&changed);
    }

    free(swap_buffer);
    swap_buffer = NULL;
    return 0;
}
