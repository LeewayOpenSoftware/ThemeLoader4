/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the UIFILE XML tokenizer (R-DUI-17)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_xml.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* Every event's spans must lie inside the input: check them as a consumer would. */
static dui_status
check_event(void *context, const dui_xml_event *ev)
{
    size_t size = *(const size_t *)context;

    if ((size_t)ev->name.offset + ev->name.length > size ||
        (size_t)ev->value.offset + ev->value.length > size ||
        ev->depth > DUI_XML_MAX_DEPTH)
    {
        __builtin_trap();
    }
    return DUI_OK;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    dui_xml_stats stats;
    size_t context = size;
    dui_status with_sink, without_sink;

    with_sink = dui_xml_parse(data, size, check_event, &context, &stats, NULL);
    without_sink = dui_xml_parse(data, size, NULL, NULL, NULL, NULL);
    /* R-GEN-8: the sink does not change the verdict */
    if (with_sink != without_sink)
        __builtin_trap();
    return 0;
}
