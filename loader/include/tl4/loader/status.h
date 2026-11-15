/*
 * PROJECT:     Theme Loader 4 - loader
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Status codes shared by every reader and validator
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#ifndef TL4_LOADER_STATUS_H
#define TL4_LOADER_STATUS_H

typedef enum tl4_status {
    TL4_OK = 0,
    TL4_E_ARG,             /* a NULL pointer or an impossible argument */
    TL4_E_TOO_LARGE,       /* the input exceeds a fixed limit (file size, counts) */
    TL4_E_TRUNCATED,       /* a structure extends past the validated size */
    TL4_E_DOS_HEADER,      /* no MZ header or e_lfanew out of bounds */
    TL4_E_PE_SIGNATURE,    /* no PE\0\0 at e_lfanew */
    TL4_E_FILE_HEADER,     /* IMAGE_FILE_HEADER rejected */
    TL4_E_OPTIONAL_HEADER, /* optional header magic, size or data directories rejected */
    TL4_E_SECTION,         /* a section header is inconsistent with the file */
    TL4_E_NO_RESOURCES,    /* no resource data directory, or none inside a section's raw data */
    TL4_E_RSRC_DIRECTORY,  /* a resource directory is out of bounds, too deep, too wide or empty */
    TL4_E_RSRC_ENTRY,      /* a directory entry points outside the resource area or at itself */
    TL4_E_RSRC_NAME,       /* a string name is out of bounds, empty or too long */
    TL4_E_RSRC_ORDER,      /* entries are not strictly ascending with names before ids (R-PE-8) */
    TL4_E_RSRC_LANGUAGES,  /* a (type, name) pair has more than one language entry (R-PE-8) */
    TL4_E_RSRC_DATA,       /* a data entry is outside the section's raw data */
    TL4_E_NOT_FOUND        /* lookup: no such resource */
} tl4_status;

const char *tl4_status_name(tl4_status status);

#endif
