/*
 * PROJECT:     Theme Loader 4 - loader
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Property model with binary values and the GetTheme* query subset (milestone L2, REQUIREMENTS section 5)
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The model is built from a style the validator accepted, into a buffer the caller sized with tl4_model_size()
 * (R-GEN-7: the core allocates nothing). It holds four flat arrays: classes (one per CMAP ordinal), part-states
 * (grouped by class, sorted by part and state), properties (grouped by part-state, sorted by name id) and the class
 * ordinals sorted by name. Every value is an (offset, size) into the style's bytes, re-checked against the mapping at
 * copy time (R-GEN-11); the model never stores a pointer into the input. Building re-runs the validator (one
 * validator, R-GEN-10) and refuses a buffer smaller than what the counts need; a second record walk fills the arrays
 * and stops at the capacity the first pass sized (R-GEN-11). Sorting is an iterative heapsort: no allocation, no
 * recursion, O(n log n) at the limits (R-MOD-1, R-GEN-6). The first of duplicate (class, part, state, name) records
 * wins (R-V4-9); a class name may repeat and a lookup by name returns the lowest ordinal (R-V4-2).
 *
 * The lookup order of tl4_model_find mirrors the engine's MSSTYLES_FindProperty (LiberNT dll/win32/uxtheme/msstyles.c):
 * (part, state) along the class and its override chain, then (part, 0), then (0, 0); the chain is the BCMAP base
 * chain (at most TL4_V4_MAX_CHAIN steps) followed by `globals` once, as the engine appends it to every chain
 * (docs/design/L2-MODEL-DESIGN.md section 4.2, an engine rule recorded as such).
 *
 * Getters copy exactly the bytes the validator checked for the record's type (R-MOD-2) and return the file's values
 * unchanged; the clamps of R-MOD-3 are applied by the caller at the boundary to the system (design note section 6).
 */
#ifndef TL4_LOADER_MODEL_H
#define TL4_LOADER_MODEL_H

#include "v4.h"

#define TL4_NO_BASE 0xFFFFFFFFu

/* Where a property was found, as the engine's PROPERTYORIGIN numbers them (uxtheme.h: PO_STATE 0, PO_PART 1,
 * PO_CLASS 2, PO_GLOBAL 3). */
enum tl4_origin { TL4_PO_STATE = 0, TL4_PO_PART = 1, TL4_PO_CLASS = 2, TL4_PO_GLOBAL = 3 };

enum tl4_area { TL4_AREA_VARIANT = 0, TL4_AREA_RMAP = 1 };

typedef struct tl4_class {
    uint32_t name_off;     /* into the CMAP resource: the UTF-16 name, name_chars code units, not terminated here */
    uint32_t name_chars;
    uint32_t base;         /* BCMAP base ordinal or TL4_NO_BASE */
    uint32_t first_ps;     /* its part-states: partstates[first_ps .. first_ps + nps) */
    uint32_t nps;
} tl4_class;

typedef struct tl4_partstate {
    uint32_t class_ordinal;
    uint16_t part, state;
    uint32_t first_prop;   /* its properties: props[first_prop .. first_prop + nprops), sorted by name_id */
    uint32_t nprops;
} tl4_partstate;

typedef struct tl4_prop {
    uint32_t class_ordinal;
    uint16_t part, state;
    uint16_t name_id;
    uint8_t type;          /* the TMT_* primitive type the validator accepted */
    uint8_t origin;        /* enum tl4_origin */
    uint8_t area;          /* enum tl4_area: which record area data_off refers to */
    uint8_t reserved_;
    uint32_t reference;    /* 0, or the id the record references (R-V4-7) */
    uint32_t data_off;     /* inline bytes inside the area, when reference == 0 */
    uint32_t cb;           /* their size, exact for the type (R-V4-6); 0 for a reference record */
    uint32_t seq;          /* file order: RMAP records first, then VARIANT (the duplicate rule, R-V4-9) */
} tl4_prop;

typedef struct tl4_model_counts {
    uint32_t classes;           /* CMAP names, descriptors included */
    uint32_t descriptors;       /* leading non-class names */
    uint32_t partstates;        /* true count of distinct (class, part, state) */
    uint32_t props;             /* properties kept */
    uint32_t duplicate_props;   /* records dropped by the first-wins rule (R-V4-9) */
    uint32_t duplicate_names;   /* class names that repeat an earlier name (R-V4-2) */
    uint32_t globals;           /* ordinal of `globals` or TL4_NO_BASE */
    uint32_t sysmetrics;        /* ordinal of `sysmetrics` or TL4_NO_BASE */
} tl4_model_counts;

typedef struct tl4_model {
    const tl4_pe *style;
    const tl4_pe *strings;      /* the explicitly opened .mui or NULL (R-RES-1) */
    uint32_t cmap_off, cmap_size;           /* the CMAP resource inside the style */
    uint32_t area_off[2], area_size[2];     /* the VARIANT and RMAP resources (enum tl4_area) */
    uint32_t nclasses;
    tl4_class *classes;
    uint32_t npartstates;
    tl4_partstate *partstates;
    uint32_t nprops;
    tl4_prop *props;
    uint32_t *name_order;                   /* nclasses ordinals sorted by name, equal names by ordinal */
    tl4_model_counts counts;
    tl4_v4_stats stats;                     /* what the validator reported for this build */
} tl4_model;

/* A parsed font string ("Segoe UI, 9, Bold, Quality:ClearType"): the only syntax the corpus uses. */
typedef struct tl4_font {
    uint16_t face[32];     /* UTF-16, NUL-terminated, at most 31 characters (R-RES-2, LF_FACESIZE) */
    uint32_t face_chars;
    int32_t points;        /* 1..255 */
    uint8_t bold, italic, underline, strikeout;
    uint8_t quality;       /* LOGFONT quality: 0 default, 1 draft, 2 proof, 3 non-antialiased, 4 antialiased,
                              5 ClearType, 6 ClearType natural */
    uint8_t reserved_[3];
} tl4_font;

/* Bytes a model for a style with these statistics needs; 0 when the statistics are impossible. */
size_t tl4_model_size(const tl4_v4_stats *stats);

/* Validate `style` (with `strings`, or NULL) and build the model into buffer[buffer_size]. The buffer must be 8-byte
 * aligned and at least tl4_model_size() of the statistics the validation returns; otherwise TL4_E_MODEL_SIZE. On any
 * failure *out is cleared (R-GEN-5). */
tl4_status tl4_model_build(const tl4_pe *style, const tl4_pe *strings, void *buffer, size_t buffer_size, tl4_model *out);

/* The lowest ordinal whose CMAP name equals the query exactly (code units, no folding). TL4_E_NOT_FOUND otherwise. */
tl4_status tl4_model_class(const tl4_model *m, const uint8_t *utf16, uint32_t chars, uint32_t *ordinal);
tl4_status tl4_model_class_ascii(const tl4_model *m, const char *name, uint32_t *ordinal);
/* The name of a class ordinal: a pointer into the CMAP and its length, re-checked against the mapping. */
tl4_status tl4_model_class_name(const tl4_model *m, uint32_t ordinal, const uint8_t **utf16, uint32_t *chars);

/* The property (name_id) for (class, part, state) in the engine's search order (header). TL4_E_NOT_FOUND when no
 * class of the chain has it. */
tl4_status tl4_model_find(const tl4_model *m, uint32_t class_ordinal, uint32_t part, uint32_t state, uint32_t name_id,
                          const tl4_prop **out);
/* Part-state (part, 0) exists in the class or its chain (IsThemePartDefined). */
int tl4_model_part_defined(const tl4_model *m, uint32_t class_ordinal, uint32_t part);

/* Getters (R-MOD-2): TL4_E_TYPE when the property is not of the asked type, TL4_E_TRUNCATED when the bytes are no
 * longer inside the mapping (R-GEN-11), TL4_E_BUFFER when the caller's buffer is too small. */
tl4_status tl4_get_int(const tl4_model *m, const tl4_prop *p, int32_t *value);          /* INT, SIZE */
tl4_status tl4_get_bool(const tl4_model *m, const tl4_prop *p, int32_t *value);         /* BOOL: 0 or 1 */
tl4_status tl4_get_enum(const tl4_model *m, const tl4_prop *p, int32_t *value);         /* ENUM */
tl4_status tl4_get_color(const tl4_model *m, const tl4_prop *p, uint32_t *colorref);    /* COLOR */
tl4_status tl4_get_position(const tl4_model *m, const tl4_prop *p, int32_t xy[2]);      /* POSITION */
tl4_status tl4_get_rect(const tl4_model *m, const tl4_prop *p, int32_t ltrb[4]);        /* RECT */
tl4_status tl4_get_margins(const tl4_model *m, const tl4_prop *p, int32_t lrtb[4]);     /* MARGINS */
/* INTLIST: up to `cap` values; *count is the record's count (at most TL4_V4_MAX_INTLIST). */
tl4_status tl4_get_intlist(const tl4_model *m, const tl4_prop *p, int32_t *values, uint32_t cap, uint32_t *count);
/* STRING, inline or by string-table reference: UTF-16 copy, NUL-terminated, *chars without the NUL. */
tl4_status tl4_get_string(const tl4_model *m, const tl4_prop *p, uint16_t *out, uint32_t cap_chars, uint32_t *chars);
/* FONT: the referenced string, parsed (TL4_E_FONT_SYNTAX when it is not the known syntax). */
tl4_status tl4_get_font(const tl4_model *m, const tl4_prop *p, tl4_font *font);
/* FILENAME / DISKSTREAM: the referenced IMAGE / STREAM id and its bytes as (offset, size) inside the style, for the
 * chunk walker (R-RES-3); the bytes are never handed to a decoder from here. */
tl4_status tl4_get_filename(const tl4_model *m, const tl4_prop *p, uint32_t *id, uint32_t *offset, uint32_t *size);
tl4_status tl4_get_diskstream(const tl4_model *m, const tl4_prop *p, uint32_t *id, uint32_t *offset, uint32_t *size);
/* GetThemeMetric's rule: the first 32-bit word of INT, SIZE, BOOL, ENUM, COLOR, POSITION (x), MARGINS (left) or
 * INTLIST (first value). */
tl4_status tl4_get_metric(const tl4_model *m, const tl4_prop *p, int32_t *value);

/* The font string parser on its own (tests, and the adapter's v3 path if it wants the same rules). */
tl4_status tl4_parse_font(const uint8_t *utf16, uint32_t chars, tl4_font *font);

#endif
