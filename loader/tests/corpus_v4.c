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
        if (sscanf(line, "%64[0-9a-f],%lu,%127[^,],%31[^,],%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu", e->sha, &e->size, e->name, e->expect,
                   &e->version, &e->cmap, &e->bcmap, &e->records, &e->rmap, &e->images, &e->streams, &e->blocks,
                   &e->partstates, &e->dup_props, &e->dup_names, &e->model_bytes, &e->unresolved_bp) == 17)
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
    unsigned long max_size;
};

static void max_u(uint32_t *m, uint32_t v) { if (v > *m) *m = v; }

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
        void *buf = need ? malloc(need) : 0;
        tl4_status mst = buf ? tl4_model_build(&pe, strings, buf, need, &model) : TL4_E_MODEL_SIZE;
        unsigned long unresolved = s.unresolved_fonts + s.unresolved_filenames + s.unresolved_diskstreams + s.unresolved_strings;
        unsigned long total = s.variant.records + s.rmap.records;
        unsigned long unresolved_bp = total ? unresolved * 10000ul / total : 0;
        if (mst != TL4_OK) { printf("%-36s %s  MODEL BUILD FAILED: %s\n", name, sha, tl4_status_name(mst)); t->failed++; free(buf); free(mdata); free(data); return; }
        if (e)
        {
            bad = (strcmp(e->expect, "ok") != 0 || e->size != size || e->cmap != s.cmap_names || e->bcmap != s.bcmap_entries ||
                   e->records != s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value ||
                   e->rmap != s.rmap.records + s.rmap.dropped_unknown_type + s.rmap.dropped_ref_on_value ||
                   e->images != s.images || e->streams != s.streams || e->blocks != s.string_blocks ||
                   e->partstates != model.counts.partstates || e->dup_props != model.counts.duplicate_props ||
                   e->dup_names != model.counts.duplicate_names || e->model_bytes != (unsigned long)need ||
                   e->unresolved_bp != unresolved_bp || model.counts.unresolved != unresolved);
            if (bad) t->mismatched++; else t->ok++;
        }
        else
            t->unknown++;
        printf("%-36s %s  ok  cmap %u (desc %u) bcmap %u depth %u  records %u (+%u unknown, +%u ref-on-value)  rmap %u  images %u streams %u blocks %u  fonts %u/%u filenames %u/%u diskstreams %u/%u strings %u/%u  partstates %u classes %u  model: partstates %u props %u dup %u dupnames %u bytes %lu globals %u sysmetrics %u unresolved %u (%lu bp)%s%s\n",
               name, sha, s.cmap_names, s.descriptors, s.bcmap_entries, s.bcmap_max_depth, s.variant.records,
               s.variant.dropped_unknown_type, s.variant.dropped_ref_on_value, s.rmap.records, s.images, s.streams, s.string_blocks,
               s.fonts - s.unresolved_fonts, s.fonts, s.filenames - s.unresolved_filenames, s.filenames,
               s.diskstreams - s.unresolved_diskstreams, s.diskstreams, s.strings - s.unresolved_strings, s.strings, s.variant.partstates, s.variant.classes,
               model.counts.partstates, model.counts.props, model.counts.duplicate_props, model.counts.duplicate_names, (unsigned long)need,
               model.counts.globals, model.counts.sysmetrics, model.counts.unresolved, unresolved_bp,
               e ? "" : "  (not in the expected table)", bad ? "  MISMATCH with the expected table" : "");
        /* a ready row for the expected table, so a regeneration copies rather than retypes */
        printf("csvrow: %s,%lu,%s,ok,4,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%lu,%lu\n", sha, (unsigned long)size, name, s.cmap_names, s.bcmap_entries,
               s.variant.records + s.variant.dropped_unknown_type + s.variant.dropped_ref_on_value,
               s.rmap.records + s.rmap.dropped_unknown_type + s.rmap.dropped_ref_on_value, s.images, s.streams, s.string_blocks,
               model.counts.partstates, model.counts.duplicate_props, model.counts.duplicate_names, (unsigned long)need, unresolved_bp);
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
    if (t.files == 0) { printf("no .msstyles under %s\n", dir); return 1; }
    return (t.failed || t.mismatched) ? 1 : 0;
}
