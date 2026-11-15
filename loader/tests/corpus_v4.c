/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Corpus runner: validates every .msstyles under TL4_CORPUS_DIR and compares with corpus-expected.csv
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Exit 77 (ctest: skipped) when TL4_CORPUS_DIR is unset. The reference files never enter the repository (R-PROC-1);
 * this program reads them where the operator put them and prints statistics only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/loader/model.h>
#include <tl4/loader/png.h>
#include <tl4/loader/strings.h>
#include <tl4/loader/range.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* ---- a compact SHA-256 (FIPS 180-4), test code only ---------------------------------------------------------- */

static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
    0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
    0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void sha256_block(uint32_t h[8], const uint8_t *p)
{
    uint32_t w[64], a, b, c, d, e, f, g, hh, i;
    for (i = 0; i < 16; i++) w[i] = ((uint32_t)p[4 * i] << 24) | ((uint32_t)p[4 * i + 1] << 16) | ((uint32_t)p[4 * i + 2] << 8) | p[4 * i + 3];
    for (i = 16; i < 64; i++)
    {
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++)
    {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), maj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + maj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

static void sha256_hex(const uint8_t *data, size_t len, char out[65])
{
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t tail[128];
    size_t i, rem = len % 64, padlen;
    uint64_t bits = (uint64_t)len * 8;
    for (i = 0; i + 64 <= len; i += 64) sha256_block(h, data + i);
    memcpy(tail, data + i, rem);
    tail[rem] = 0x80;
    padlen = (rem < 56) ? 64 : 128;
    memset(tail + rem + 1, 0, padlen - rem - 1);
    for (i = 0; i < 8; i++) tail[padlen - 1 - i] = (uint8_t)(bits >> (8 * i));
    sha256_block(h, tail);
    if (padlen == 128) sha256_block(h, tail + 64);
    for (i = 0; i < 8; i++) snprintf(out + 8 * i, 9, "%08x", h[i]);
}

/* ---- expected table ------------------------------------------------------------------------------------------ */

struct expected {
    char sha[65];
    unsigned long size, version, cmap, bcmap, records, rmap, images, streams, blocks;
    unsigned long partstates, dup_props, dup_names, model_bytes;   /* the model's columns (L2) */
    unsigned long unresolved_bp;   /* unresolved references in basis points of the records (R-V4-12 allows 100) */
    unsigned long images_ok, max_pixels, max_idat, pixels_total;   /* the chunk walker's columns (L3) */
    char name[128];
    char expect[32];     /* "ok" or the status name the validator must return */
};

static struct expected table[128];
static int ntable = 0;

static int load_expected(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[512];
    if (!f) return 0;
    while (fgets(line, sizeof(line), f))
    {
        struct expected *e = &table[ntable];
        if (line[0] == '#' || line[0] == 's' || line[0] == '\n' || ntable >= 128) continue;
        if (sscanf(line, "%64[0-9a-f],%lu,%127[^,],%31[^,],%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu", e->sha, &e->size, e->name, e->expect,
                   &e->version, &e->cmap, &e->bcmap, &e->records, &e->rmap, &e->images, &e->streams, &e->blocks,
                   &e->partstates, &e->dup_props, &e->dup_names, &e->model_bytes, &e->unresolved_bp,
                   &e->images_ok, &e->max_pixels, &e->max_idat, &e->pixels_total) == 21)
            ntable++;
    }
    fclose(f);
    return ntable;
}

/* ---- files ----------------------------------------------------------------------------------------------------- */

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    buf = (uint8_t *)malloc(n ? (size_t)n : 1);
    if (!buf) { fclose(f); return 0; }
    if (n && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 0; }
    fclose(f);
    *size = (size_t)n;
    return buf;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix), i;
    if (a < b) return 0;
    for (i = 0; i < b; i++)
    {
        char x = s[a - b + i], y = suffix[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (x != y) return 0;
    }
    return 1;
}

struct totals {
    int files, ok, mismatched, failed, unknown;
    tl4_v4_stats max;
    tl4_strings_stats max_str;                     /* R-RES-8: blocks per file, string length, languages per block */
    unsigned long str_entries, str_strings;        /* over every table of every file */
    unsigned long range_checked, range_rejected, range_errors;   /* R-MOD-7: every property through the range function */
    unsigned long max_size;
    unsigned long max_pixels, max_idat, max_chunks, max_side, images_failed;   /* over every image of every style */
};

/* Every IMAGE and STREAM resource through the chunk walker (R-RES-3/4/5) */
struct image_walk { unsigned long ok, failed, max_pixels, max_idat, max_chunks, max_side, pixels_total; tl4_status first_failure; };
static int image_cb(const tl4_resource *res, void *context)
{
    struct image_walk *w = (struct image_walk *)context;
    tl4_res_name image = tl4_res_ascii("IMAGE"), stream = tl4_res_ascii("STREAM");
    tl4_png_info info;
    tl4_status st;
    if (!tl4_res_name_equal(&res->type, &image) && !tl4_res_name_equal(&res->type, &stream)) return 0;
    st = tl4_png_check(res->data, res->size, &info);
    if (st != TL4_OK) { if (!w->failed) w->first_failure = st; w->failed++; return 0; }
    w->ok++;
    if (info.pixels > w->max_pixels) w->max_pixels = (unsigned long)info.pixels;
    if (info.idat_bytes > w->max_idat) w->max_idat = info.idat_bytes;
    if (info.chunks > w->max_chunks) w->max_chunks = info.chunks;
    if (info.width > w->max_side) w->max_side = info.width;
    if (info.height > w->max_side) w->max_side = info.height;
    w->pixels_total += (unsigned long)info.pixels;
    return 0;
}

static void max_u(uint32_t *m, uint32_t v) { if (v > *m) *m = v; }

/* R-MOD-7: what the range function rejected on the corpus, by property, and what the getters refused, by type and
 * status: numbers only, the reviewer decides the table from them */
struct range_reject { uint32_t type, name_id, row; unsigned long count; int32_t min, max; };
struct range_error { uint32_t type; tl4_status status; unsigned long count; };
static struct range_reject rejects[64];
static struct range_error errors[64];
static unsigned nrejects, nerrors, rejects_dropped, errors_dropped;

static void range_note_reject(uint32_t type, uint32_t name_id, unsigned row, int32_t value)
{
    unsigned i;
    for (i = 0; i < nrejects; i++)
        if (rejects[i].type == type && rejects[i].name_id == name_id)
        {
            rejects[i].count++;
            if (value < rejects[i].min) rejects[i].min = value;
            if (value > rejects[i].max) rejects[i].max = value;
            return;
        }
    if (nrejects == sizeof(rejects) / sizeof(rejects[0])) { rejects_dropped++; return; }
    rejects[nrejects].type = type; rejects[nrejects].name_id = name_id; rejects[nrejects].row = row;
    rejects[nrejects].count = 1; rejects[nrejects].min = value; rejects[nrejects].max = value;
    nrejects++;
}

static void range_note_error(uint32_t type, tl4_status status)
{
    unsigned i;
    for (i = 0; i < nerrors; i++)
        if (errors[i].type == type && errors[i].status == status) { errors[i].count++; return; }
    if (nerrors == sizeof(errors) / sizeof(errors[0])) { errors_dropped++; return; }
    errors[nerrors].type = type; errors[nerrors].status = status; errors[nerrors].count = 1;
    nerrors++;
}

static void range_report(void)
{
    unsigned i;
    for (i = 0; i < nrejects; i++)
        printf("  rejected: type %u property %u (row %u): %lu values, min %ld, max %ld\n", rejects[i].type, rejects[i].name_id,
               rejects[i].row, rejects[i].count, (long)rejects[i].min, (long)rejects[i].max);
    for (i = 0; i < nerrors; i++)
        printf("  getter error: type %u -> %s: %lu properties\n", errors[i].type, tl4_status_name(errors[i].status), errors[i].count);
    if (rejects_dropped || errors_dropped) printf("  (%u reject kinds and %u error kinds beyond the table)\n", rejects_dropped, errors_dropped);
}

static void check_file(const char *path, const char *name, struct totals *t)
{
    size_t size, msize = 0;
    uint8_t *data = read_file(path, &size), *mdata = 0;
    tl4_pe pe, mui, *strings = 0;
    tl4_v4_stats s;
    tl4_status st;
    char sha[65], muipath[2048];
    const struct expected *e = 0;
    int i;

    t->files++;
    if (!data) { printf("%-36s read failed\n", name); t->failed++; return; }
    if (size > t->max_size) t->max_size = (unsigned long)size;
    sha256_hex(data, size, sha);
    for (i = 0; i < ntable; i++) if (strcmp(table[i].sha, sha) == 0) { e = &table[i]; break; }

    st = tl4_pe_open(data, size, &pe);
    if (st == TL4_OK)
    {
        /* the stock styles keep their strings in <dir>/en-US/<name>.mui: open it explicitly (R-RES-1) */
        size_t dirlen = strlen(path) - strlen(name);
        if (dirlen + strlen(name) + 12 < sizeof(muipath))
        {
            memcpy(muipath, path, dirlen);
            snprintf(muipath + dirlen, sizeof(muipath) - dirlen, "en-US/%s.mui", name);
            mdata = read_file(muipath, &msize);
            if (mdata && tl4_pe_open(mdata, msize, &mui) == TL4_OK) strings = &mui;
        }
        st = tl4_v4_validate(&pe, strings, &s);
    }
    if (st != TL4_OK)
    {
        int expected_reject = e && strcmp(e->expect, tl4_status_name(st)) == 0;
        printf("%-36s %s  REJECTED: %s%s\n", name, sha, tl4_status_name(st), expected_reject ? "  (as the expected table says)" : "");
        if (expected_reject) t->ok++; else t->failed++;
    }
    else
    {
        int bad = 0;
        tl4_model model;
        size_t need = tl4_model_size(&s);
        memset(&model, 0, sizeof(model));
        void *buf = need ? malloc(need) : 0;
        tl4_status mst = buf ? tl4_model_build(&pe, strings, buf, need, &model) : TL4_E_MODEL_SIZE;
        unsigned long unresolved = s.unresolved_fonts + s.unresolved_filenames + s.unresolved_diskstreams + s.unresolved_strings;
        unsigned long total = s.variant.records + s.rmap.records;
        unsigned long unresolved_bp = total ? unresolved * 10000ul / total : 0;
        struct image_walk w;
        int stopped = 0;
        memset(&w, 0, sizeof(w));
        if (tl4_pe_enum(&pe, image_cb, &w, &stopped) != TL4_OK) w.failed++;
        if (mst != TL4_OK) { printf("%-36s %s  MODEL BUILD FAILED: %s\n", name, sha, tl4_status_name(mst)); t->failed++; free(buf); free(mdata); free(data); return; }
        {
            /* R-MOD-7 / section 6.1: every property of the model through its getter and the range function; a rejected
             * corpus value is reported (ids only) and goes to the reviewer: the table changes by commit, never the code */
            uint32_t k, rej = 0, errs = 0, chk = 0;
            for (k = 0; k < model.nprops; k++)
            {
                const tl4_prop *p = &model.props[k];
                int32_t v[4], list[TL4_RANGE_INTLIST_MAX];
                uint32_t c = 0, id = 0, off = 0, sz = 0, chars = 0, col = 0;
                uint16_t str[512];
                tl4_font fnt;
                tl4_status gs = TL4_OK, rs;
                switch (p->type)
                {
                    case TL4_V4_INT: case TL4_V4_SIZE: gs = tl4_get_int(&model, p, v); c = 1; break;
                    case TL4_V4_BOOL: gs = tl4_get_bool(&model, p, v); c = 1; break;
                    case TL4_V4_ENUM: gs = tl4_get_enum(&model, p, v); c = 1; break;
                    case TL4_V4_COLOR: gs = tl4_get_color(&model, p, &col); v[0] = (int32_t)col; c = 1; break;
                    case TL4_V4_POSITION: gs = tl4_get_position(&model, p, v); c = 2; break;
                    case TL4_V4_RECT: gs = tl4_get_rect(&model, p, v); c = 4; break;
                    case TL4_V4_MARGINS: gs = tl4_get_margins(&model, p, v); c = 4; break;
                    case TL4_V4_INTLIST: gs = tl4_get_intlist(&model, p, list, TL4_RANGE_INTLIST_MAX, &c); break;
                    case TL4_V4_STRING: gs = tl4_get_string(&model, p, str, 512, &chars); break;
                    case TL4_V4_FONT: gs = tl4_get_font(&model, p, &fnt); break;
                    case TL4_V4_FILENAME: gs = tl4_get_filename(&model, p, &id, &off, &sz); break;
                    case TL4_V4_DISKSTREAM: gs = tl4_get_diskstream(&model, p, &id, &off, &sz); break;
                    default: break;                       /* STREAM, BITMAPREF, HBITMAP, FLOAT...: the range row alone */
                }
                if (gs != TL4_OK) { errs++; range_note_error(p->type, gs); continue; }
                chk++;
                if (p->type == TL4_V4_STRING) rs = tl4_range_check_string(chars);
                else if (p->type == TL4_V4_FONT) rs = tl4_range_check_font(&fnt);
                else if (p->type == TL4_V4_INTLIST) rs = tl4_range_check(p->type, p->name_id, list, c);
                else rs = tl4_range_check(p->type, p->name_id, v, c);
                if (rs != TL4_OK)
                {
                    /* the offending value: the first component or entry outside its row, as a number only */
                    enum tl4_range_row row = tl4_range_row(p->type, p->name_id);
                    const int32_t *vals = p->type == TL4_V4_INTLIST ? list : v;
                    uint32_t n = p->type == TL4_V4_STRING ? 0 : p->type == TL4_V4_FONT ? 0 : c, j;
                    int32_t offending = p->type == TL4_V4_STRING ? (int32_t)chars : p->type == TL4_V4_FONT ? fnt.points : 0;
                    for (j = 0; j < n; j++) if (tl4_range_check(p->type, p->name_id, &vals[j], 1) != TL4_OK || row == TL4_RANGE_NEVER) { offending = vals[j]; break; }
                    if (rej++ < 3)
                        printf("%-36s range: REJECTED type %u property %u (class %u part %u state %u, row %u) value %ld\n", "",
                               p->type, p->name_id, p->class_ordinal, p->part, p->state, (unsigned)row, (long)offending);
                    range_note_reject(p->type, p->name_id, (unsigned)row, offending);
                }
            }
            printf("%-36s range: %u properties checked, %u rejected, %u getter errors\n", "", chk, rej, errs);
            t->range_checked += chk; t->range_rejected += rej; t->range_errors += errs;
        }
        if (e)
        {
            bad = (strcmp(e->expect, "ok") != 0 || e->size != size || e->cmap != s.cmap_names || e->bcmap != s.bcmap_entries ||
                   e->records != s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value ||
                   e->rmap != s.rmap.records + s.rmap.dropped_unknown_type + s.rmap.dropped_ref_on_value ||
                   e->images != s.images || e->streams != s.streams || e->blocks != s.string_blocks ||
                   e->partstates != model.counts.partstates || e->dup_props != model.counts.duplicate_props ||
                   e->dup_names != model.counts.duplicate_names || e->model_bytes != (unsigned long)need ||
                   e->unresolved_bp != unresolved_bp || model.counts.unresolved != unresolved ||
                   e->images_ok != w.ok || e->max_pixels != w.max_pixels || e->max_idat != w.max_idat || e->pixels_total != w.pixels_total);
            if (bad) t->mismatched++; else t->ok++;
        }
        else
            t->unknown++;
        printf("%-36s %s  ok  cmap %u (desc %u) bcmap %u depth %u  records %u (+%u unknown, +%u ref-on-value)  rmap %u  images %u streams %u blocks %u  fonts %u/%u filenames %u/%u diskstreams %u/%u strings %u/%u  partstates %u classes %u  model: partstates %u props %u dup %u dupnames %u bytes %lu globals %u sysmetrics %u unresolved %u (%lu bp)  png: ok %lu failed %lu%s%s max %lu px, %lu idat, %lu chunks, side %lu, total %lu px%s%s\n",
               name, sha, s.cmap_names, s.descriptors, s.bcmap_entries, s.bcmap_max_depth, s.variant.records,
               s.variant.dropped_unknown_type, s.variant.dropped_ref_on_value, s.rmap.records, s.images, s.streams, s.string_blocks,
               s.fonts - s.unresolved_fonts, s.fonts, s.filenames - s.unresolved_filenames, s.filenames,
               s.diskstreams - s.unresolved_diskstreams, s.diskstreams, s.strings - s.unresolved_strings, s.strings, s.variant.partstates, s.variant.classes,
               model.counts.partstates, model.counts.props, model.counts.duplicate_props, model.counts.duplicate_names, (unsigned long)need,
               model.counts.globals, model.counts.sysmetrics, model.counts.unresolved, unresolved_bp,
               w.ok, w.failed, w.failed ? " (first: " : "", w.failed ? tl4_status_name(w.first_failure) : "",
               w.max_pixels, w.max_idat, w.max_chunks, w.max_side, w.pixels_total,
               e ? "" : "  (not in the expected table)", bad ? "  MISMATCH with the expected table" : "");
        /* a ready row for the expected table, so a regeneration copies rather than retypes */
        {
            /* R-RES-8 statistics of the tables the validator just accepted: the style's, and the module's when one was opened */
            tl4_strings_stats ss, sm;
            memset(&sm, 0, sizeof(sm));
            if (tl4_strings_check(&pe, &ss) != TL4_OK || (strings && tl4_strings_check(strings, &sm) != TL4_OK))
                printf("%-36s string table check failed after a successful validation\n", name);
            printf("%-36s strings: style blocks %u entries %u strings %u max %u chars (id %u) languages %u; module blocks %u entries %u strings %u max %u chars (id %u) languages %u\n",
                   "", ss.blocks, ss.entries, ss.strings, ss.max_chars, ss.longest_id, ss.max_languages,
                   sm.blocks, sm.entries, sm.strings, sm.max_chars, sm.longest_id, sm.max_languages);
            max_u(&t->max_str.blocks, ss.blocks + sm.blocks);
            max_u(&t->max_str.max_chars, ss.max_chars); max_u(&t->max_str.max_chars, sm.max_chars);
            max_u(&t->max_str.max_languages, ss.max_languages); max_u(&t->max_str.max_languages, sm.max_languages);
            t->str_entries += ss.entries + sm.entries;
            t->str_strings += ss.strings + sm.strings;
        }
        printf("csvrow: %s,%lu,%s,ok,4,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu\n", sha, (unsigned long)size, name, s.cmap_names, s.bcmap_entries,
               s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value,
               s.rmap.records + s.rmap.dropped_unknown_type + s.rmap.dropped_ref_on_value, s.images, s.streams, s.string_blocks,
               model.counts.partstates, model.counts.duplicate_props, model.counts.duplicate_names, (unsigned long)need, unresolved_bp,
               w.ok, w.max_pixels, w.max_idat, w.pixels_total);
        if (w.failed) t->images_failed += w.failed;
        if (w.max_pixels > t->max_pixels) t->max_pixels = w.max_pixels;
        if (w.max_idat > t->max_idat) t->max_idat = w.max_idat;
        if (w.max_chunks > t->max_chunks) t->max_chunks = w.max_chunks;
        if (w.max_side > t->max_side) t->max_side = w.max_side;
        free(buf);
        max_u(&t->max.cmap_names, s.cmap_names); max_u(&t->max.bcmap_max_depth, s.bcmap_max_depth);
        max_u(&t->max.variant.records, s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value);
        max_u(&t->max.variant.partstates, s.variant.partstates); max_u(&t->max.variant.classes, s.variant.classes);
        max_u(&t->max.variant.max_cb, s.variant.max_cb); max_u(&t->max.variant.max_part, s.variant.max_part);
        max_u(&t->max.variant.max_state, s.variant.max_state); max_u(&t->max.images, s.images); max_u(&t->max.streams, s.streams);
        max_u(&t->max.rmap.records, s.rmap.records);
    }
    free(mdata);
    free(data);
}

static void scan(const char *dir, struct totals *t, int depth)
{
    char path[2048];
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h;
    snprintf(path, sizeof(path), "%s\\*", dir);
    h = FindFirstFileA(path, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        if (fd.cFileName[0] == '.') continue;
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (depth < 16) scan(path, t, depth + 1); }
        else if (ends_with(fd.cFileName, ".msstyles")) check_file(path, fd.cFileName, t);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    struct dirent *ent;
    if (!d) return;
    while ((ent = readdir(d)) != NULL)
    {
        struct stat sb;
        if (ent->d_name[0] == '.') continue;
        snprintf(path, sizeof(path), "%s/%s", dir, ent->d_name);
        if (stat(path, &sb) != 0) continue;
        if (S_ISDIR(sb.st_mode)) { if (depth < 16) scan(path, t, depth + 1); }
        else if (ends_with(ent->d_name, ".msstyles")) check_file(path, ent->d_name, t);
    }
    closedir(d);
#endif
}

int main(int argc, char **argv)
{
    const char *dir = getenv("TL4_CORPUS_DIR");
    const char *expected = argc > 1 ? argv[1] : "corpus-expected.csv";
    struct totals t;

    if (!dir || !*dir) { printf("TL4_CORPUS_DIR is not set: corpus run skipped\n"); return 77; }
    memset(&t, 0, sizeof(t));
    if (!load_expected(expected)) printf("warning: no expected table at %s; statistics only\n", expected);
    scan(dir, &t, 0);
    printf("\n%d files: %d as the expected table says, %d mismatch, %d not in the table, %d rejected unexpectedly\n", t.files, t.ok, t.mismatched, t.unknown, t.failed);
    printf("maxima: file %lu bytes, CMAP names %u, BCMAP depth %u, VARIANT records %u, RMAP records %u, part-states %u, classes %u, cbData %u, part %u, state %u, images %u, streams %u\n",
           t.max_size, t.max.cmap_names, t.max.bcmap_max_depth, t.max.variant.records, t.max.rmap.records, t.max.variant.partstates,
           t.max.variant.classes, t.max.variant.max_cb, t.max.variant.max_part, t.max.variant.max_state, t.max.images, t.max.streams);
    printf("images: %lu failed the chunk walker; maxima: %lu pixels, %lu compressed bytes, %lu chunks, side %lu\n",
           t.images_failed, t.max_pixels, t.max_idat, t.max_chunks, t.max_side);
    printf("string tables: %lu block entries, %lu strings over every table; maxima: blocks per file %u, string %u characters, languages per block %u\n",
           t.str_entries, t.str_strings, t.max_str.blocks, t.max_str.max_chars, t.max_str.max_languages);
    printf("ranges (section 6.1): %lu properties checked, %lu rejected, %lu getter errors%s\n",
           t.range_checked, t.range_rejected, t.range_errors, t.range_rejected ? "  <-- a rejected corpus value goes to the reviewer" : "");
    range_report();
    if (t.files == 0) { printf("no .msstyles under %s\n", dir); return 1; }
    return (t.failed || t.mismatched || t.images_failed) ? 1 : 0;
}
