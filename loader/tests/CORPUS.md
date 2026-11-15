# Reference corpus (local only)

Compatibility tests read a local directory of reference styles given by the environment variable `TL4_CORPUS_DIR`
(or the CMake cache variable of the same name). The directory is scanned recursively for `*.msstyles`. Nothing in it
is ever copied into this repository (R-PROC-1): `.gitignore` refuses `.msstyles`, `.mui`, `shellstyle*.dll` and
`.theme`.

When `TL4_CORPUS_DIR` is not set the corpus runner exits with 77, which ctest reports as skipped, so a clean
checkout builds and passes with the generated fixtures alone.

The expected statistics per file (format version, class count, record count, image count, hashes) come from the
format analysis in `docs/design/` and are kept as plain numbers in `loader/tests/corpus-expected.csv` once the
corpus runner exists (milestone L1).
