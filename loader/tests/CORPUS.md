# Reference corpus (local only)

The corpus runner `tl4_loader_corpus_v4` reads a local directory of reference styles given by the environment
variable `TL4_CORPUS_DIR`, scanned recursively for `*.msstyles`. For a style whose strings live in a `.mui`, it opens
`<dir>/en-US/<name>.mui` next to the style explicitly (R-RES-1); no MUI redirection is involved. Nothing from that
directory is ever copied into this repository (R-PROC-1): `.gitignore` refuses `.msstyles`, `.mui`,
`shellstyle*.dll` and `.theme`.

When `TL4_CORPUS_DIR` is not set the runner exits with 77, which ctest reports as skipped, so a clean checkout
builds and passes with the generated fixtures alone. To run it:

```
TL4_CORPUS_DIR=/path/to/styles ctest --test-dir <build> -R corpus --output-on-failure
```

`corpus-expected.csv` holds the expected statistics per file, identified by sha256 (name informational): size,
`PACKTHEM_VERSION`, CMAP names, BCMAP entries, VARIANT records (accepted + dropped), RMAP records, IMAGE and STREAM
resources, inline string-table blocks, and from the model (L2): true part-states, duplicate records dropped
(R-V4-9), duplicate class names (R-V4-2), model bytes. The numbers come from the format analysis of 2026-11-15 and are plain
metadata. The runner prints one line per file, the per-file statistics the validator returned, whether they match the
table, and the maxima over the run against the limits of `docs/security/REQUIREMENTS.md` section 6 (R-V4-13). A
rejected or mismatching file fails the test.
