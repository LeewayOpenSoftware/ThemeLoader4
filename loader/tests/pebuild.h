/*
 * PROJECT:     Theme Loader 4 - loader tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Fixture builder: tiny PE images with a resource section, built in memory
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * Every test input is generated here (R-PROC-1); no Microsoft or third-party file is ever part of the tests.
 */
#ifndef TL4_LOADER_PEBUILD_H
#define TL4_LOADER_PEBUILD_H

#include <stddef.h>
#include <stdint.h>

#define PB_MAX_RESOURCES 64

/* One resource. A NULL name string means "use the id". */
typedef struct pb_res {
    const char *type_name;
    uint32_t type_id;
    const char *name_name;
    uint32_t name_id;
    uint32_t language;
    const uint8_t *data;
    uint32_t size;
} pb_res;

/* Where things ended up, for tests that corrupt specific fields. All values are file offsets unless named. */
typedef struct pb_layout {
    uint32_t e_lfanew;
    uint32_t file_header;
    uint32_t optional_header;
    uint32_t data_directory;     /* first data directory entry */
    uint32_t section_header;     /* the .rsrc section header */
    uint32_t rsrc_raw;           /* PointerToRawData of .rsrc = file offset of the root directory */
    uint32_t rsrc_va;            /* VirtualAddress of .rsrc */
    uint32_t rsrc_raw_size;
    uint32_t root_entries;       /* file offset of the first root directory entry */
    uint32_t first_data_entry;   /* file offset of the first IMAGE_RESOURCE_DATA_ENTRY */
    uint32_t first_data;         /* file offset of the first resource payload */
    uint32_t total;              /* bytes written */
} pb_layout;

typedef struct pb_options {
    int pe32plus;                /* 1: PE32+ (amd64), 0: PE32 (i386) */
    uint32_t rsrc_va;            /* 0 = default 0x1000 */
    uint32_t extra_sections;     /* empty sections added before .rsrc (0..4) */
    int unsorted;                /* 1: keep the input order of entries (to build ordering violations) */
} pb_options;

/* Build the image into out[cap]. Entries are emitted in the order the PE format requires (names first by code unit,
 * then ids ascending; languages ascending) unless opt->unsorted. Returns the bytes written, 0 on error. */
size_t pb_build(const pb_res *res, size_t count, const pb_options *opt, uint8_t *out, size_t cap, pb_layout *layout);

/* Little-endian stores and loads used by tests to corrupt fields. */
void pb_put16(uint8_t *p, uint32_t v);
void pb_put32(uint8_t *p, uint32_t v);
uint32_t pb_get32(const uint8_t *p);

#endif
