/*
 * PROJECT:     Theme Loader 4 - trust decisions
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     libFuzzer harness for tl4_trust_sd_writable (by Jim 2): no crash on any bytes; the component-owner flag
 *              only adds trust and never changes "unreadable"; safe as a directory implies safe as a file and above root
 * COPYRIGHT:   Copyright 2026 Leeway Open Software
 */
#include <tl4/trust/trust.h>
#include <stdlib.h>
#include <string.h>
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t *copy = size ? malloc(size) : NULL;
    int o;
    if (size) memcpy(copy, data, size);
    for (o = 0; o < 3; o++) {
        tl4_trust_reason r0 = tl4_trust_sd_writable(copy, size, (enum tl4_trust_object)o, 0);
        tl4_trust_reason r1 = tl4_trust_sd_writable(copy, size, (enum tl4_trust_object)o, 1);
        if (r0 == TL4_TRUST_SAFE && r1 != TL4_TRUST_SAFE) abort();
        if ((r0 == TL4_TRUST_W_UNREADABLE) != (r1 == TL4_TRUST_W_UNREADABLE)) abort();
    }
    if (tl4_trust_sd_writable(copy, size, TL4_TRUST_DIR, 1) == TL4_TRUST_SAFE &&
        (tl4_trust_sd_writable(copy, size, TL4_TRUST_FILE, 1) != TL4_TRUST_SAFE ||
         tl4_trust_sd_writable(copy, size, TL4_TRUST_DIR_ABOVE_ROOT, 1) != TL4_TRUST_SAFE)) abort();
    (void)tl4_trust_sid_length(copy, size);
    free(copy);
    return 0;
}
