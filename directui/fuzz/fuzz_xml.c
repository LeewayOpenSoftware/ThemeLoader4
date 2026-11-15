/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the UIFILE XML tokenizer (R-DUI-17, S1-FUZZ-PLAN 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */

#include <tl4/directui/dui_xml.h>
#include "dui_fuzz_check.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct trace
{
    size_t size;
    uint64_t hash;      /* FNV-1a over every event field, in order */
    uint32_t events;
} trace;

static void
mix(trace *t, uint64_t v)
{
    unsigned i;

    for (i = 0; i < 8; i++)
    {
        t->hash ^= (v >> (8 * i)) & 0xFFu;
        t->hash *= 1099511628211ULL;
    }
}

/* 1. ranges: every span lies inside the input; the depth is within its limit. */
static dui_status
record(void *context, const dui_xml_event *ev)
{
    trace *t = context;

    FUZZ_CHECK((size_t)ev->name.offset + ev->name.length <= t->size);
    FUZZ_CHECK((size_t)ev->value.offset + ev->value.length <= t->size);
    FUZZ_CHECK(ev->depth <= DUI_XML_MAX_DEPTH);
    mix(t, (uint64_t)ev->kind);
    mix(t, ((uint64_t)ev->name.offset << 32) | ev->name.length);
    mix(t, ((uint64_t)ev->value.offset << 32) | ev->value.length);
    mix(t, ev->depth);
    t->events++;
    return DUI_OK;
}

static dui_status
run(const uint8_t *data, size_t size, trace *t, dui_xml_stats *stats, size_t *off)
{
    memset(t, 0, sizeof(*t));
    memset(stats, 0, sizeof(*stats));
    t->size = size;
    t->hash = 1469598103934665603ULL;
    return dui_xml_parse(data, size, record, t, stats, off);
}

static int
stats_eq(const dui_xml_stats *a, const dui_xml_stats *b)
{
    return a->elements == b->elements && a->attributes == b->attributes && a->max_depth == b->max_depth &&
           a->max_attrs_per_element == b->max_attrs_per_element && a->max_name_len == b->max_name_len &&
           a->max_value_len == b->max_value_len && a->comments == b->comments && a->text_runs == b->text_runs;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    trace t1, t2, t3;
    dui_xml_stats s1, s2, s3;
    size_t o1 = 0, o2 = 0, o3 = 0;
    dui_status r1, r2, r3, without_sink;
    uint8_t *copy;

    r1 = run(data, size, &t1, &s1, &o1);

    /* 2. counts: the statistics are within the limits and agree with the events */
    if (r1 == DUI_OK)
    {
        FUZZ_CHECK(s1.elements <= DUI_XML_MAX_ELEMENTS && s1.attributes <= DUI_XML_MAX_ATTRS_TOTAL &&
                   s1.max_depth <= DUI_XML_MAX_DEPTH && s1.max_attrs_per_element <= DUI_XML_MAX_ATTRS_PER_ELEM &&
                   s1.max_name_len <= DUI_XML_MAX_NAME_LEN && s1.max_value_len <= DUI_XML_MAX_VALUE_LEN);
        FUZZ_CHECK(t1.events == 2 * s1.elements + s1.attributes + s1.text_runs);
    }

    /* 3. agreement: the verdict does not depend on the sink (R-GEN-8) */
    without_sink = dui_xml_parse(data, size, NULL, NULL, NULL, NULL);
    FUZZ_CHECK(without_sink == r1);

    /* 5. determinism: the same bytes again give the same status, events and statistics */
    r2 = run(data, size, &t2, &s2, &o2);
    FUZZ_CHECK(r2 == r1 && o2 == o1 && t2.hash == t1.hash && t2.events == t1.events && stats_eq(&s1, &s2));

    /* 6. copy independence: an exact-size copy gives the same result */
    copy = fuzz_exact_copy(data, size);
    r3 = run(copy, size, &t3, &s3, &o3);
    FUZZ_CHECK(r3 == r1 && o3 == o1 && t3.hash == t1.hash && stats_eq(&s1, &s3));
    free(copy);
    return 0;
}
