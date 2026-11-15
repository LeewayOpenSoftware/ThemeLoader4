/*
 * PROJECT:     Theme Loader 4 - loader
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Bounded PE / resource reader (REQUIREMENTS section 3)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The reader works on a byte range the caller already holds in memory: (base, size). The size MUST come from the
 * same object that was mapped (the open handle or the mapping itself), never from a path lookup made earlier
 * (R-POL-2). The reader never allocates (R-GEN-7), reads input only through bounded little-endian helpers (R-GEN-2),
 * computes every offset in 64 bits (R-GEN-3) and validates the whole resource tree in tl4_pe_open() before any
 * lookup is answered. It accepts only the shape a style needs: a tree of exactly three levels (type, name,
 * language) with strictly ordered entries and one language per (type, name) (R-PE-6, R-PE-8), whose directories,
 * entries, names and data all lie inside the raw data of the section that holds the resource directory (R-PE-5,
 * R-PE-9). Anything else is rejected, never skipped (R-GEN-5). Nothing else in the file is parsed (R-PE-10).
 */
#ifndef TL4_LOADER_PE_H
#define TL4_LOADER_PE_H

#include <stddef.h>
#include <stdint.h>
#include "status.h"

/* Limits (REQUIREMENTS section 6; corpus maxima: file 2,976,256 bytes, 4 sections, 1,611 entries in one
 * directory, names of 20 characters). */
#define TL4_PE_MAX_FILE_SIZE     (32u * 1024u * 1024u)
#define TL4_PE_MAX_SECTIONS      32u
#define TL4_PE_MAX_DIR_ENTRIES   16384u  /* named + id entries in one directory (R-PE-7) */
#define TL4_PE_MAX_ENTRIES       65536u  /* directory entries visited per file (R-PE-7) */
#define TL4_PE_MAX_NAME_CHARS    64u     /* UTF-16 code units of a string name (R-PE-7) */
#define TL4_PE_DEPTH             3u      /* type / name / language (R-PE-6) */

typedef struct tl4_pe {
    const uint8_t *base;
    size_t size;
    uint16_t machine;
    uint16_t nsections;
    uint8_t is_pe32plus;
    uint8_t reserved_[3];
    uint32_t section_table_off;     /* file offset of the first section header */
    uint32_t rsrc_rva;              /* resource data directory RVA (= the root directory) */
    uint32_t rsrc_dir_size;         /* resource data directory size as declared */
    uint32_t rsrc_section_va;       /* the section that holds the resource directory */
    uint32_t rsrc_section_raw;      /* its PointerToRawData */
    uint32_t rsrc_section_raw_size; /* its SizeOfRawData */
    uint32_t rsrc_off;              /* file offset of the root directory */
    uint32_t rsrc_limit;            /* bytes from the root directory to the end of the section's raw data */
    /* statistics from the validating walk (R-GEN-7: counts only, nothing kept) */
    uint32_t ndirectories;
    uint32_t nentries;
    uint32_t nresources;
} tl4_pe;

enum tl4_name_kind {
    TL4_NAME_ID = 0,      /* a 16-bit ordinal, as MAKEINTRESOURCE */
    TL4_NAME_UTF16 = 1,   /* a UTF-16LE string of `length` code units inside the image, not terminated */
    TL4_NAME_ASCII = 2    /* a caller string, compared with UTF-16 names by ordinal ASCII case folding */
};

typedef struct tl4_res_name {
    uint8_t kind;                /* enum tl4_name_kind */
    uint8_t reserved_[3];
    uint32_t id;                 /* TL4_NAME_ID */
    uint32_t length;             /* TL4_NAME_UTF16: code units; TL4_NAME_ASCII: bytes */
    const uint8_t *utf16;        /* TL4_NAME_UTF16 */
    const char *ascii;           /* TL4_NAME_ASCII; must stay valid while the name is used */
} tl4_res_name;

typedef struct tl4_resource {
    tl4_res_name type;
    tl4_res_name name;
    uint32_t language;
    uint32_t rva;                /* of the data */
    uint32_t offset;             /* file offset of the data (R-PE-9): [offset, offset + size) lies inside the
                                  * raw data of the resource section, hence inside the file */
    uint32_t size;
    const uint8_t *data;         /* base + offset, for convenience; derived from the checked offset only */
} tl4_resource;

tl4_res_name tl4_res_id(uint32_t id);
tl4_res_name tl4_res_ascii(const char *ascii);
/* ids equal by value; strings equal by ordinal ASCII case folding; an id never equals a string (R-DUI-9 style) */
int tl4_res_name_equal(const tl4_res_name *a, const tl4_res_name *b);

/* Validate the headers and the whole resource tree. On success every later call only re-walks validated memory. */
tl4_status tl4_pe_open(const uint8_t *base, size_t size, tl4_pe *pe);

/* Enumerate every resource. The callback returns 0 to continue, nonzero to stop; the nonzero value is reported
 * through `stopped` when it is not NULL. */
typedef int (*tl4_res_callback)(const tl4_resource *res, void *context);
tl4_status tl4_pe_enum(const tl4_pe *pe, tl4_res_callback callback, void *context, int *stopped);

/* Find the resource (type, name); there is exactly one language entry (R-PE-8). */
tl4_status tl4_pe_find(const tl4_pe *pe, const tl4_res_name *type, const tl4_res_name *name, tl4_resource *out);

/* Count the name entries under a type (0 when the type is absent). */
uint32_t tl4_pe_count_type(const tl4_pe *pe, const tl4_res_name *type);

#endif
