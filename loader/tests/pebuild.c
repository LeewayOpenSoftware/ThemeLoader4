/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder: tiny PE images with a resource section, built in memory
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include "pebuild.h"
#include <string.h>

#define HEADERS_SIZE 0x200u

void pb_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
void pb_put32(uint8_t *p, uint32_t v) { pb_put16(p, v & 0xFFFFu); pb_put16(p + 2, v >> 16); }
uint32_t pb_get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* A key: a string name or an id. */
struct key {
    const char *name;
    uint32_t id;
};

static int key_equal(struct key a, struct key b)
{
    if (a.name || b.name) return a.name && b.name && strcmp(a.name, b.name) == 0;
    return a.id == b.id;
}

/* PE order: names before ids, names by code unit (ASCII here), ids ascending. */
static int key_less(struct key a, struct key b)
{
    if (a.name && !b.name) return 1;
    if (!a.name && b.name) return 0;
    if (a.name) return strcmp(a.name, b.name) < 0;
    return a.id < b.id;
}

static uint32_t str_len(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* One name directory: the resources (indices) that share type and name, i.e. the language entries. */
struct name_group {
    struct key key;
    uint32_t items[PB_MAX_RESOURCES];
    uint32_t count;
};

struct type_group {
    struct key key;
    struct name_group names[PB_MAX_RESOURCES];
    uint32_t count;
};

static void sort_keys(struct key *keys, uint32_t *perm, uint32_t n)
{
    uint32_t i, j;
    for (i = 1; i < n; i++)
    {
        uint32_t p = perm[i];
        for (j = i; j > 0 && key_less(keys[p], keys[perm[j - 1]]); j--) perm[j] = perm[j - 1];
        perm[j] = p;
    }
}

static uint32_t write_string(uint8_t *r, uint32_t *str_cur, const char *s)
{
    uint32_t len = str_len(s), c, field = 0x80000000u | *str_cur;
    pb_put16(r + *str_cur, len);
    for (c = 0; c < len; c++) pb_put16(r + *str_cur + 2 + 2 * c, (uint8_t)s[c]);
    *str_cur += 2 + 2 * len;
    return field;
}

size_t pb_build(const pb_res *res, size_t count, const pb_options *opt, uint8_t *out, size_t cap, pb_layout *layout)
{
    static struct type_group types[PB_MAX_RESOURCES];   /* static: the groups are large for a stack frame */
    pb_options o = {1, 0, 0, 0};
    uint32_t ntypes = 0, i, j, t;
    uint32_t dir_bytes, str_bytes, data_bytes, raw_size, rsrc_va, rsrc_raw, nsections, sh_off, opt_size;
    uint32_t tperm[PB_MAX_RESOURCES];
    uint8_t *p;

    if (opt) o = *opt;
    if (o.rsrc_va == 0) o.rsrc_va = 0x1000;
    if (count == 0 || count > PB_MAX_RESOURCES || o.extra_sections > 4) return 0;
    memset(types, 0, sizeof(types));

    /* group: types in order of first appearance, names under each, languages in input order */
    for (i = 0; i < count; i++)
    {
        struct key tk = {res[i].type_name, res[i].type_id}, nk = {res[i].name_name, res[i].name_id};
        struct type_group *tg;
        for (t = 0; t < ntypes; t++) if (key_equal(types[t].key, tk)) break;
        if (t == ntypes) { types[ntypes].key = tk; ntypes++; }
        tg = &types[t];
        for (j = 0; j < tg->count; j++) if (key_equal(tg->names[j].key, nk)) break;
        if (j == tg->count) { tg->names[j].key = nk; tg->count++; }
        tg->names[j].items[tg->names[j].count++] = i;
    }

    /* sizes */
    dir_bytes = 16 + 8 * ntypes;
    str_bytes = 0;
    data_bytes = 0;
    for (t = 0; t < ntypes; t++)
    {
        dir_bytes += 16 + 8 * types[t].count;
        if (types[t].key.name) str_bytes += 2 + 2 * str_len(types[t].key.name);
        for (j = 0; j < types[t].count; j++)
        {
            dir_bytes += 16 + 8 * types[t].names[j].count;
            if (types[t].names[j].key.name) str_bytes += 2 + 2 * str_len(types[t].names[j].key.name);
        }
    }
    dir_bytes += 16 * (uint32_t)count;                   /* data entries */
    str_bytes = (str_bytes + 7u) & ~7u;
    for (i = 0; i < count; i++) data_bytes += (res[i].size + 7u) & ~7u;

    raw_size = (dir_bytes + str_bytes + data_bytes + 0x1FFu) & ~0x1FFu;
    rsrc_va = o.rsrc_va;
    rsrc_raw = HEADERS_SIZE;
    if ((size_t)rsrc_raw + raw_size > cap) return 0;
    memset(out, 0, (size_t)rsrc_raw + raw_size);

    /* headers */
    p = out;
    p[0] = 'M'; p[1] = 'Z';
    pb_put32(p + 0x3C, 0x40);
    p = out + 0x40;
    p[0] = 'P'; p[1] = 'E'; p[2] = 0; p[3] = 0;
    nsections = 1 + o.extra_sections;
    pb_put16(p + 4, o.pe32plus ? 0x8664u : 0x14Cu);      /* Machine */
    pb_put16(p + 6, nsections);                          /* NumberOfSections */
    opt_size = o.pe32plus ? 240u : 224u;
    pb_put16(p + 20, opt_size);                          /* SizeOfOptionalHeader */
    pb_put16(p + 22, 0x2102u);                           /* Characteristics */
    p = out + 0x40 + 24;                                 /* optional header */
    pb_put16(p, o.pe32plus ? 0x20Bu : 0x10Bu);
    pb_put32(p + 32, 0x1000);                            /* SectionAlignment */
    pb_put32(p + 36, 0x200);                             /* FileAlignment */
    pb_put32(p + 56, rsrc_va + ((raw_size + 0xFFFu) & ~0xFFFu)); /* SizeOfImage */
    pb_put32(p + 60, HEADERS_SIZE);                      /* SizeOfHeaders */
    pb_put16(p + 68, 2);                                 /* Subsystem */
    pb_put32(p + (o.pe32plus ? 108u : 92u), 16);         /* NumberOfRvaAndSizes */
    {
        uint32_t dd = o.pe32plus ? 112u : 96u;
        pb_put32(p + dd + 2 * 8, rsrc_va);               /* resource directory RVA */
        pb_put32(p + dd + 2 * 8 + 4, dir_bytes);         /* resource directory size */
        if (layout) layout->data_directory = 0x40 + 24 + dd;
    }
    sh_off = 0x40 + 24 + opt_size;
    if (sh_off + nsections * 40 > HEADERS_SIZE) return 0;
    for (i = 0; i < o.extra_sections; i++)
    {
        uint8_t *sh = out + sh_off + i * 40;
        memcpy(sh, ".pad", 4);
        pb_put32(sh + 8, 0x10);                          /* VirtualSize */
        pb_put32(sh + 12, 0x100000u + 0x1000u * i);      /* VirtualAddress, away from .rsrc */
        pb_put32(sh + 36, 0x40000040u);
    }
    {
        uint8_t *sh = out + sh_off + o.extra_sections * 40;
        memcpy(sh, ".rsrc", 5);
        pb_put32(sh + 8, dir_bytes + str_bytes + data_bytes); /* VirtualSize */
        pb_put32(sh + 12, rsrc_va);
        pb_put32(sh + 16, raw_size);
        pb_put32(sh + 20, rsrc_raw);
        pb_put32(sh + 36, 0x40000040u);
        if (layout) layout->section_header = sh_off + o.extra_sections * 40;
    }

    /* resource tree: root, then the type directories, then the name directories, then data entries, strings, data */
    {
        uint8_t *r = out + rsrc_raw;
        uint32_t str_cur = dir_bytes;
        uint32_t data_cur = dir_bytes + str_bytes;
        uint32_t dataent_cur = dir_bytes - 16 * (uint32_t)count;
        uint32_t tdir = 16 + 8 * ntypes;                 /* the first type directory */
        uint32_t ldir;                                   /* the first language directory */
        uint32_t data_index = 0, named_types = 0;
        struct key tkeys[PB_MAX_RESOURCES];

        for (t = 0; t < ntypes; t++) { tkeys[t] = types[t].key; tperm[t] = t; if (types[t].key.name) named_types++; }
        if (!o.unsorted) sort_keys(tkeys, tperm, ntypes);

        ldir = tdir;
        for (t = 0; t < ntypes; t++) ldir += 16 + 8 * types[t].count;

        pb_put16(r + 12, named_types);
        pb_put16(r + 14, ntypes - named_types);
        if (layout) layout->root_entries = rsrc_raw + 16;

        for (t = 0; t < ntypes; t++)
        {
            struct type_group *tg = &types[tperm[t]];
            uint32_t this_tdir = tdir, k, named_names = 0, nperm[PB_MAX_RESOURCES];
            struct key nkeys[PB_MAX_RESOURCES];
            for (k = 0; k < t; k++) this_tdir += 16 + 8 * types[tperm[k]].count;

            pb_put32(r + 16 + 8 * t, tg->key.name ? write_string(r, &str_cur, tg->key.name) : tg->key.id);
            pb_put32(r + 16 + 8 * t + 4, 0x80000000u | this_tdir);

            for (j = 0; j < tg->count; j++) { nkeys[j] = tg->names[j].key; nperm[j] = j; if (tg->names[j].key.name) named_names++; }
            if (!o.unsorted) sort_keys(nkeys, nperm, tg->count);
            pb_put16(r + this_tdir + 12, named_names);
            pb_put16(r + this_tdir + 14, tg->count - named_names);

            for (j = 0; j < tg->count; j++)
            {
                struct name_group *ng = &tg->names[nperm[j]];
                uint32_t l, lperm[PB_MAX_RESOURCES];

                pb_put32(r + this_tdir + 16 + 8 * j, ng->key.name ? write_string(r, &str_cur, ng->key.name) : ng->key.id);
                pb_put32(r + this_tdir + 16 + 8 * j + 4, 0x80000000u | ldir);

                for (l = 0; l < ng->count; l++) lperm[l] = l;
                if (!o.unsorted)
                    for (l = 1; l < ng->count; l++)
                    {
                        uint32_t q = lperm[l], m;
                        for (m = l; m > 0 && res[ng->items[q]].language < res[ng->items[lperm[m - 1]]].language; m--) lperm[m] = lperm[m - 1];
                        lperm[m] = q;
                    }
                pb_put16(r + ldir + 14, ng->count);
                for (l = 0; l < ng->count; l++)
                {
                    const pb_res *rs = &res[ng->items[lperm[l]]];
                    pb_put32(r + ldir + 16 + 8 * l, rs->language);
                    pb_put32(r + ldir + 16 + 8 * l + 4, dataent_cur);
                    pb_put32(r + dataent_cur, rsrc_va + data_cur);
                    pb_put32(r + dataent_cur + 4, rs->size);
                    if (rs->size) memcpy(r + data_cur, rs->data, rs->size);
                    if (layout && data_index == 0) { layout->first_data_entry = rsrc_raw + dataent_cur; layout->first_data = rsrc_raw + data_cur; }
                    data_index++;
                    dataent_cur += 16;
                    data_cur += (rs->size + 7u) & ~7u;
                }
                ldir += 16 + 8 * ng->count;
            }
        }
    }

    if (layout)
    {
        layout->e_lfanew = 0x40;
        layout->file_header = 0x44;
        layout->optional_header = 0x40 + 24;
        layout->rsrc_raw = rsrc_raw;
        layout->rsrc_va = rsrc_va;
        layout->rsrc_raw_size = raw_size;
        layout->total = rsrc_raw + raw_size;
    }
    return (size_t)rsrc_raw + raw_size;
}
