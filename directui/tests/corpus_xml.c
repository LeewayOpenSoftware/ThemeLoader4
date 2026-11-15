/*
 * PROJECT:     Theme Loader 4 - DirectUI
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Corpus runner: tokenize reference UIFILE resources and report the maxima
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 *
 * The reference files never enter the repository (R-PROC-1). Until the bounded PE/resource
 * reader of L0 exists, the runner reads resources extracted outside the repository:
 * $TL4_CORPUS_DIR/uifile.list names one extracted resource file per line (relative to
 * TL4_CORPUS_DIR). Without TL4_CORPUS_DIR the test is skipped (exit code 77).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tl4/directui/dui_layout.h>
#include <tl4/directui/dui_xml.h>

#define MAX_PATH_LEN 1024

static int
read_file(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = fopen(path, "rb");
    long len;

    if (!f)
        return 0;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0 ||
        (unsigned long)len > DUI_XML_MAX_SIZE + 1u)
    {
        fclose(f);
        return 0;
    }
    *size = (size_t)len;
    *data = malloc(*size ? *size : 1);
    if (!*data || fread(*data, 1, *size, f) != *size)
    {
        free(*data);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

static void
keep_max(uint32_t *a, uint32_t b)
{
    if (b > *a)
        *a = b;
}

int
main(void)
{
    const char *dir = getenv("TL4_CORPUS_DIR");
    char path[MAX_PATH_LEN], line[MAX_PATH_LEN];
    FILE *list;
    dui_xml_stats max = { 0 };
    unsigned total = 0, rejected = 0;
    size_t max_size = 0;

    if (!dir || !dir[0])
    {
        printf("TL4_CORPUS_DIR not set: skipped\n");
        return 77;
    }
    if (snprintf(path, sizeof(path), "%s/uifile.list", dir) >= (int)sizeof(path) || !(list = fopen(path, "r")))
    {
        fprintf(stderr, "cannot open %s/uifile.list\n", dir);
        return 1;
    }
    while (fgets(line, sizeof(line), list))
    {
        uint8_t *data = NULL;
        size_t size = 0, off = 0;
        dui_xml_stats st;
        dui_status status;

        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        if (snprintf(path, sizeof(path), "%s/%s", dir, line) >= (int)sizeof(path) || !read_file(path, &data, &size))
        {
            fprintf(stderr, "cannot read %s\n", line);
            fclose(list);
            return 1;
        }
        total++;
        status = dui_xml_parse(data, size, NULL, NULL, &st, &off);
        if (status == DUI_OK)
        {
            /* every resource also builds as a consumer layout (R-DUI-4 entry point) */
            dui_layout layout;
            status = dui_layout_load(data, size, &layout, &off);
            dui_layout_free(&layout);
        }
        if (status != DUI_OK)
        {
            rejected++;
            printf("REJECT %s: %s at offset %zu\n", line, dui_status_name(status), off);
        }
        else
        {
            keep_max(&max.elements, st.elements);
            keep_max(&max.attributes, st.attributes);
            keep_max(&max.max_depth, st.max_depth);
            keep_max(&max.max_attrs_per_element, st.max_attrs_per_element);
            keep_max(&max.max_name_len, st.max_name_len);
            keep_max(&max.max_value_len, st.max_value_len);
            keep_max(&max.comments, st.comments);
            keep_max(&max.text_runs, st.text_runs);
            if (size > max_size)
                max_size = size;
        }
        free(data);
    }
    fclose(list);

    printf("resources %u, rejected %u\n", total, rejected);
    printf("maxima per resource: size %zu, elements %u, attributes %u, depth %u, "
           "attributes per element %u, name length %u, value length %u, comments %u, text runs %u\n",
           max_size, max.elements, max.attributes, max.max_depth, max.max_attrs_per_element,
           max.max_name_len, max.max_value_len, max.comments, max.text_runs);
    return rejected ? 1 : 0;
}
