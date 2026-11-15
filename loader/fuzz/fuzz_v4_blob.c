/*
 * PROJECT:     Theme Loader 4 - loader fuzz harnesses
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for the validator's pieces on one raw blob (S1 plan: fuzz_v4_blob)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The first input byte selects the piece: 0 CMAP (then every name is fetched), 1 VMAP, 2 BCMAP (the second byte is
 * the CMAP count), 3 records (the second byte is the CMAP count), 4 string table (the blob is a PE image and the
 * second byte chooses the string ids asked of tl4_v4_find_string). Every promise of the API is asserted.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/v4.h>

#define PROMISE(cond) do { if (!(cond)) abort(); } while (0)

static int walk_equal(const tl4_v4_walk_stats *a, const tl4_v4_walk_stats *b)
{
    return a->records == b->records && a->dropped_unknown_type == b->dropped_unknown_type &&
           a->dropped_ref_on_value == b->dropped_ref_on_value && a->partstates == b->partstates &&
           a->classes == b->classes && a->max_part == b->max_part && a->max_state == b->max_state && a->max_cb == b->max_cb;
}

static int count_cb(const tl4_v4_record *r, void *ctx)
{
    uint32_t *n = (uint32_t *)ctx;
    volatile uint8_t sink = 0;
    PROMISE(r->type_id >= 200 && r->type_id <= 255 && r->part_id <= 255 && r->state_id <= 255);
    if (r->data && r->cb_data) { sink = r->data[0]; sink = r->data[r->cb_data - 1]; }
    PROMISE(r->reference == 0 || r->data == NULL);
    (void)sink;
    (*n)++;
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *copy;
    uint32_t len;
    if (size < 2 || size - 2 > 0xFFFFFFFFu) return 0;
    len = (uint32_t)(size - 2);
    /* a private copy of exactly the blob's size: an over-read by one byte faults (S1 plan point 6) */
    copy = (uint8_t *)malloc(len ? len : 1);
    if (!copy) return 0;
    memcpy(copy, data + 2, len);

    switch (data[0] % 5)
    {
        case 0:
        {
            tl4_v4_cmap cm, cm2;
            tl4_status st = tl4_v4_check_cmap(copy, len, &cm), st2 = tl4_v4_check_cmap(copy, len, &cm2);
            PROMISE(st == st2 && cm.count == cm2.count && cm.size == cm2.size && cm.data == cm2.data);   /* R-GEN-8 */
            if (st == TL4_OK)
            {
                uint32_t i; const uint8_t *s; uint32_t chars;
                PROMISE(cm.count >= 1 && cm.count <= TL4_V4_MAX_CLASSES);
                for (i = 0; i < cm.count; i++)
                {
                    PROMISE(tl4_v4_cmap_name(&cm, i, &s, &chars) == TL4_OK);
                    PROMISE(chars >= 1 && s >= copy && (size_t)(s - copy) + 2u * chars <= len);
                }
                PROMISE(tl4_v4_cmap_name(&cm, cm.count, &s, &chars) == TL4_E_NOT_FOUND);
            }
            else
                PROMISE(cm.count == 0 && cm.data == NULL);                        /* R-GEN-5 */
            break;
        }
        case 1:
        {
            tl4_v4_vmap vm;
            tl4_status st = tl4_v4_check_vmap(copy, len, &vm);
            if (st == TL4_OK)
                PROMISE(vm.variant && vm.variant_chars <= TL4_V4_MAX_VMAP_CHARS && (size_t)(vm.color - copy) + 2u * vm.color_chars <= len);
            else
                PROMISE(vm.variant == NULL && vm.variant_chars == 0);
            break;
        }
        case 2:
        {
            tl4_v4_bcmap bm;
            uint32_t cmap_count = (uint32_t)data[1] + 1u;
            tl4_status st = tl4_v4_check_bcmap(copy, len, cmap_count, &bm);
            if (st == TL4_OK)
            {
                uint32_t i, base;
                PROMISE(bm.count <= cmap_count && bm.max_depth <= TL4_V4_MAX_CHAIN);
                for (i = 0; i < cmap_count; i++)
                {
                    PROMISE(tl4_v4_bcmap_base(&bm, i, &base) == TL4_OK);
                    PROMISE(base == 0xFFFFFFFFu || (base < cmap_count && base >= bm.shift));
                }
            }
            else
                PROMISE(bm.count == 0 && bm.data == NULL);
            break;
        }
        case 3:
        {
            tl4_v4_walk_stats ws, ws2;
            uint32_t n = 0, n2 = 0, cmap_count = (uint32_t)data[1] + 1u;
            tl4_status st = tl4_v4_walk_records(copy, len, cmap_count, TL4_V4_MAX_VARIANT_BYTES, TL4_V4_MAX_RECORDS, count_cb, &n, &ws);
            tl4_status st2 = tl4_v4_walk_records(copy, len, cmap_count, TL4_V4_MAX_VARIANT_BYTES, TL4_V4_MAX_RECORDS, count_cb, &n2, &ws2);
            PROMISE(st == st2 && n == n2 && walk_equal(&ws, &ws2));              /* R-GEN-8 */
            if (st == TL4_OK)
            {
                PROMISE(ws.records == n && ws.records <= TL4_V4_MAX_RECORDS);
                PROMISE(ws.partstates <= TL4_V4_MAX_PARTSTATES && ws.classes <= TL4_V4_MAX_CLASSES);
                PROMISE(ws.max_cb <= TL4_V4_MAX_CBDATA && ws.max_part <= 255 && ws.max_state <= 255);
            }
            else
                PROMISE(ws.records == 0 && ws.partstates == 0);                  /* R-GEN-5 */
            break;
        }
        default:
        {
            /* the string table through the reader: ids below, inside and above the fixture's block, one over 16 bits */
            tl4_pe pe;
            uint32_t ids[4], i;
            ids[0] = data[1];
            ids[1] = (uint32_t)data[1] * 257u;
            ids[2] = 496u + (data[1] & 15u);
            ids[3] = 0x10000u + data[1];
            if (tl4_pe_open(copy, len, &pe) != TL4_OK) { PROMISE(pe.base == NULL); break; }
            for (i = 0; i < 4; i++)
            {
                const uint8_t *s, *s2; uint32_t chars, chars2;
                tl4_status st = tl4_v4_find_string(&pe, ids[i], &s, &chars);
                tl4_status st2 = tl4_v4_find_string(&pe, ids[i], &s2, &chars2);
                PROMISE(st == st2 && s == s2 && chars == chars2);                /* R-GEN-8 */
                if (st == TL4_OK)
                    PROMISE(chars >= 1 && chars <= TL4_V4_MAX_FONT_CHARS && s >= copy && (size_t)(s - copy) + 2u * chars <= len);
                else
                    PROMISE(s == NULL && chars == 0);                            /* R-GEN-5 */
                if (ids[i] > 0xFFFFu) PROMISE(st == TL4_E_ARG);
            }
            break;
        }
    }
    free(copy);
    return 0;
}

#ifdef TL4_FUZZ_STANDALONE
#include <stdio.h>
int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        FILE *f = fopen(argv[i], "rb");
        long len;
        uint8_t *buf;
        if (!f) { fprintf(stderr, "cannot open %s\n", argv[i]); return 2; }
        if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 2; }
        buf = (uint8_t *)malloc(len ? (size_t)len : 1);
        if (!buf) { fclose(f); return 2; }
        if (len && fread(buf, 1, (size_t)len, f) != (size_t)len) { free(buf); fclose(f); return 2; }
        fclose(f);
        LLVMFuzzerTestOneInput(buf, (size_t)len);
        free(buf);
    }
    printf("fuzz_v4_blob: %d inputs, no broken promise\n", argc - 1);
    return 0;
}
#endif
