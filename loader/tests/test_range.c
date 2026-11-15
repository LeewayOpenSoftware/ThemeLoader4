/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     The value ranges of REQUIREMENTS section 6.1 (R-MOD-3, R-MOD-7): the lists against the table as written,
 *              the row rule, every boundary, the counts, the font and string rules
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <stdio.h>
#include <string.h>
#include <tl4/loader/range.h>
#include <tl4/loader/v4.h>

static int failures = 0, checks = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_STATUS(expr, want) do { tl4_status st_ = (expr); checks++; if (st_ != (want)) { failures++; \
    printf("FAIL %s:%d: %s -> %s, wanted %s\n", __FILE__, __LINE__, #expr, tl4_status_name(st_), tl4_status_name(want)); } } while (0)
#define N(a) ((uint32_t)(sizeof(a) / sizeof((a)[0])))

/* Section 6.1 as written, transcribed row by row with the numbers of vssym32.h. This is the oracle: a change here is a
 * change of the table and goes through its review. */
static const uint32_t T_DIVISOR[] = { 2401 /* IMAGECOUNT */, 2426 /* FRAMESPERSECOND */ };
static const uint32_t T_INDEX[] = { 2418 /* GLYPHINDEX */ };
static const uint32_t T_LEVEL[] = { 2402 /* ALPHALEVEL */, 2406, 2407, 2408, 2409, 2410 /* GRADIENTRATIO1..5 */, 2413 /* SATURATION */,
                                    2415 /* ALPHATHRESHOLD */, 2429 /* GLOWINTENSITY */, 2430 /* OPACITY */, 2432 /* COLORIZATIONOPACITY */ };
static const uint32_t T_TIME[] = { 2428 /* ANIMATIONDELAY */, 5006 /* ANIMATIONDURATION */, 6000 /* TRANSITIONDURATIONS */ };
static const uint32_t T_DPI[] = { 2420, 2421, 2422, 2423, 2424 /* MINDPI1..5 */, 2433 /* MINDPI6 */, 2434 /* MINDPI7 */ };
static const uint32_t T_METRIC[] = { 1201, 1202, 1203, 1204, 1205, 1206, 1207, 1208, 1209, 1210 /* SIZINGBORDERWIDTH .. PADDEDBORDERWIDTH */ };

static void check_list(enum tl4_range_row row, const uint32_t *want, uint32_t n)
{
    const uint32_t *got = 0;
    uint32_t m = tl4_range_members(row, &got), i;
    CHECK(m == n && got != 0);
    if (m == n && got)
    {
        for (i = 0; i < n; i++) CHECK(got[i] == want[i]);
        for (i = 1; i < n; i++) CHECK(got[i] > got[i - 1]);                                  /* sorted, no duplicate */
        for (i = 0; i < n; i++) CHECK(tl4_range_row(TL4_V4_INT, want[i]) == row);            /* the property row wins over INT */
    }
}

static void test_lists(void)
{
    const uint32_t *ids = (const uint32_t *)1;
    check_list(TL4_RANGE_DIVISOR, T_DIVISOR, N(T_DIVISOR));
    check_list(TL4_RANGE_INDEX, T_INDEX, N(T_INDEX));
    check_list(TL4_RANGE_LEVEL, T_LEVEL, N(T_LEVEL));
    check_list(TL4_RANGE_TIME, T_TIME, N(T_TIME));
    check_list(TL4_RANGE_DPI, T_DPI, N(T_DPI));
    check_list(TL4_RANGE_METRIC, T_METRIC, N(T_METRIC));
    CHECK(tl4_range_members(TL4_RANGE_PIXEL, &ids) == 0 && ids == 0);
    CHECK(tl4_range_members(TL4_RANGE_BOOL, &ids) == 0 && tl4_range_members(TL4_RANGE_NEVER, &ids) == 0 && tl4_range_members(TL4_RANGE_NONE, 0) == 0);
    /* the lists are disjoint: no property is in two rows */
    {
        const uint32_t *a, *b;
        uint32_t na, nb, i, j;
        enum tl4_range_row ra, rb;
        for (ra = TL4_RANGE_DIVISOR; ra <= TL4_RANGE_METRIC; ra++)
            for (rb = ra + 1; rb <= TL4_RANGE_METRIC; rb++)
            {
                na = tl4_range_members(ra, &a); nb = tl4_range_members(rb, &b);
                for (i = 0; i < na; i++) for (j = 0; j < nb; j++) CHECK(a[i] != b[j]);
            }
    }
}

static void test_rows(void)
{
    CHECK(tl4_range_row(TL4_V4_INT, 2416) == TL4_RANGE_PIXEL);          /* WIDTH */
    CHECK(tl4_range_row(TL4_V4_SIZE, 2411) == TL4_RANGE_PIXEL);
    CHECK(tl4_range_row(TL4_V4_POSITION, 3401) == TL4_RANGE_PIXEL);
    CHECK(tl4_range_row(TL4_V4_RECT, 5002) == TL4_RANGE_PIXEL);
    CHECK(tl4_range_row(TL4_V4_MARGINS, 3601) == TL4_RANGE_PIXEL);
    CHECK(tl4_range_row(TL4_V4_INTLIST, 1) == TL4_RANGE_PIXEL);
    CHECK(tl4_range_row(TL4_V4_INTLIST, 6000) == TL4_RANGE_TIME);       /* TRANSITIONDURATIONS: the property row wins */
    CHECK(tl4_range_row(TL4_V4_INT, 1201) == TL4_RANGE_METRIC);         /* narrower than the pixel row */
    CHECK(tl4_range_row(TL4_V4_INT, 2401) == TL4_RANGE_DIVISOR);
    CHECK(tl4_range_row(TL4_V4_BOOL, 2401) == TL4_RANGE_DIVISOR);       /* a property row wins over any type row */
    CHECK(tl4_range_row(TL4_V4_BOOL, 2201) == TL4_RANGE_BOOL);
    CHECK(tl4_range_row(TL4_V4_ENUM, 4001) == TL4_RANGE_ENUM);
    CHECK(tl4_range_row(TL4_V4_COLOR, 3801) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_STRING, 601) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_FONT, 801) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_FILENAME, 3001) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_DISKSTREAM, 8000) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_STREAM, 8000) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_BITMAPREF, 8002) == TL4_RANGE_NONE);
    CHECK(tl4_range_row(TL4_V4_HBITMAP, 2) == TL4_RANGE_NEVER);
    CHECK(tl4_range_row(TL4_V4_FLOAT, 2401) == TL4_RANGE_NEVER);        /* never, even for a listed property */
    CHECK(tl4_range_row(TL4_V4_FLOATLIST, 1) == TL4_RANGE_NEVER);
    CHECK(tl4_range_row(0, 1) == TL4_RANGE_NEVER);
    CHECK(tl4_range_row(999, 2416) == TL4_RANGE_NEVER);
}

static void boundary(uint32_t type, uint32_t id, int32_t lo, int32_t hi)
{
    int32_t v;
    v = lo - 1; CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_E_RANGE);
    v = lo;     CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_OK);
    v = hi;     CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_OK);
    v = hi + 1; CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_E_RANGE);
    v = INT32_MIN; CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_E_RANGE);
    v = INT32_MAX; CHECK_STATUS(tl4_range_check(type, id, &v, 1), TL4_E_RANGE);
}

static void test_values(void)
{
    int32_t v, a[4], list[TL4_RANGE_INTLIST_MAX + 1];
    uint32_t i;

    boundary(TL4_V4_INT, 2416, -8192, 8192);
    boundary(TL4_V4_SIZE, 2411, -8192, 8192);
    boundary(TL4_V4_INT, 2401, 1, 256);
    boundary(TL4_V4_INT, 2426, 1, 256);
    boundary(TL4_V4_INT, 2418, 0, 255);
    boundary(TL4_V4_INT, 2402, 0, 255);
    boundary(TL4_V4_INT, 2430, 0, 255);
    boundary(TL4_V4_INT, 2428, 0, 65535);
    boundary(TL4_V4_INT, 5006, 0, 65535);
    boundary(TL4_V4_INT, 2420, 1, 8192);
    boundary(TL4_V4_INT, 2434, 1, 8192);
    boundary(TL4_V4_INT, 1201, 0, 512);
    boundary(TL4_V4_INT, 1210, 0, 512);
    boundary(TL4_V4_BOOL, 2201, 0, 1);
    boundary(TL4_V4_ENUM, 4001, 0, 255);

    /* components: each one is checked */
    a[0] = 0; a[1] = 8193;
    CHECK_STATUS(tl4_range_check(TL4_V4_POSITION, 3401, a, 2), TL4_E_RANGE);
    a[1] = -8192;
    CHECK_STATUS(tl4_range_check(TL4_V4_POSITION, 3401, a, 2), TL4_OK);
    a[0] = 1; a[1] = 2; a[2] = 3; a[3] = -8193;
    CHECK_STATUS(tl4_range_check(TL4_V4_RECT, 5002, a, 4), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(TL4_V4_MARGINS, 3601, a, 4), TL4_E_RANGE);
    a[3] = 8192;
    CHECK_STATUS(tl4_range_check(TL4_V4_RECT, 5002, a, 4), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_MARGINS, 3601, a, 4), TL4_OK);

    /* INTLIST: 0..402 entries, each in its row (pixel, or time for TRANSITIONDURATIONS) */
    for (i = 0; i < N(list); i++) list[i] = 7;
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, list, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, list, TL4_RANGE_INTLIST_MAX), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, list, TL4_RANGE_INTLIST_MAX + 1), TL4_E_RANGE);
    list[200] = 8193;
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, list, 300), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, list, 200), TL4_OK);
    list[200] = 65535;
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 6000, list, 300), TL4_OK);
    list[200] = 65536;
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 6000, list, 300), TL4_E_RANGE);
    list[200] = -1;
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 6000, list, 300), TL4_E_RANGE);

    /* COLOR and the reference types: no range */
    v = -1;
    CHECK_STATUS(tl4_range_check(TL4_V4_COLOR, 3801, &v, 1), TL4_OK);
    v = INT32_MIN;
    CHECK_STATUS(tl4_range_check(TL4_V4_COLOR, 3801, &v, 1), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_FILENAME, 3001, 0, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_DISKSTREAM, 8000, 0, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_STREAM, 8000, 0, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_BITMAPREF, 8002, 0, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_STRING, 601, 0, 0), TL4_OK);         /* the string rule is tl4_range_check_string */
    CHECK_STATUS(tl4_range_check(TL4_V4_FONT, 801, 0, 0), TL4_OK);           /* the font rule is tl4_range_check_font */

    /* always out of range */
    v = 0;
    CHECK_STATUS(tl4_range_check(TL4_V4_HBITMAP, 2, &v, 1), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(TL4_V4_HBITMAP, 2, 0, 0), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(TL4_V4_FLOAT, 1, &v, 1), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(TL4_V4_FLOATLIST, 1, 0, 0), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check(999, 2416, &v, 1), TL4_E_RANGE);

    /* counts that do not fit the type, and NULL values with a count, are argument errors */
    v = 1;
    CHECK_STATUS(tl4_range_check(TL4_V4_INT, 2416, &v, 0), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_INT, 2416, a, 2), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_POSITION, 3401, a, 1), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_RECT, 5002, a, 2), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_BOOL, 2201, 0, 1), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, 0, 1), TL4_E_ARG);
    CHECK_STATUS(tl4_range_check(TL4_V4_INTLIST, 1, 0, 0), TL4_OK);
    CHECK_STATUS(tl4_range_check(TL4_V4_FILENAME, 3001, &v, 1), TL4_E_ARG);
}

static void test_font_string(void)
{
    tl4_font f;
    memset(&f, 0, sizeof(f));
    f.face_chars = 6; f.points = 9;
    CHECK_STATUS(tl4_range_check_font(&f), TL4_OK);
    f.face_chars = 0; CHECK_STATUS(tl4_range_check_font(&f), TL4_E_RANGE);
    f.face_chars = 31; CHECK_STATUS(tl4_range_check_font(&f), TL4_OK);
    f.face_chars = 32; CHECK_STATUS(tl4_range_check_font(&f), TL4_E_RANGE);
    f.face_chars = 6;
    f.points = 0; CHECK_STATUS(tl4_range_check_font(&f), TL4_E_RANGE);
    f.points = 1; CHECK_STATUS(tl4_range_check_font(&f), TL4_OK);
    f.points = 255; CHECK_STATUS(tl4_range_check_font(&f), TL4_OK);
    f.points = 256; CHECK_STATUS(tl4_range_check_font(&f), TL4_E_RANGE);
    f.points = -1; CHECK_STATUS(tl4_range_check_font(&f), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check_font(0), TL4_E_ARG);

    CHECK_STATUS(tl4_range_check_string(0), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check_string(1), TL4_OK);
    CHECK_STATUS(tl4_range_check_string(255), TL4_OK);
    CHECK_STATUS(tl4_range_check_string(256), TL4_E_RANGE);
    CHECK_STATUS(tl4_range_check_string(0xFFFFFFFFu), TL4_E_RANGE);
}

int main(void)
{
    test_lists();
    test_rows();
    test_values();
    test_font_string();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
