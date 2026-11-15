# S1: fuzz campaign plan

Status: design. Owner: security review. Normative text is `REQUIREMENTS.md`; this file says how S1 tests it.

S1 is the gate of R-PROC-3 (no L2 before the S1 report) and R-DUI-18 (D1 is not used on files we did not build before its campaign report and review are closed). Its result is `docs/security/S1-REPORT.md`.

## 1. Goal

Show, for every parser that reads style bytes, that no input causes memory-unsafe behaviour, unbounded time or memory, a broken promise of the API, or a result that differs between two runs. A clean run is evidence, not proof; the report states coverage and what was not reached.

## 2. Targets

| Harness | Entry point | Requirements | State |
|---|---|---|---|
| `fuzz_pe` | `tl4_pe_open`, `enum`, `find`, `count_type` | R-PE-1..11, R-GEN | exists (L0) |
| `fuzz_xml` | `dui_xml_parse` | R-DUI-5..9 | exists (D1) |
| `fuzz_value` | `dui_value_parse` | R-DUI-11..16 | exists (D1) |
| `fuzz_sheet` | `dui_sheets_load`, colour resolution | R-DUI-1..4, 10 | exists (D1) |
| `fuzz_layout` | `dui_layout_load` | R-DUI-4, 16 | exists (D1) |
| `fuzz_v4_blob` | the L1 validator on one raw `CMAP` / `VARIANT` / `RMAP` blob, selected by the first input byte | R-V4-1..13, R-MOD | to write with L1 |
| `fuzz_v4_style` | PE image to model, end to end, through `tl4_pe_open` | R-V4, R-GEN-10 | to write with L1 |
| `fuzz_strings` | string-table reader | R-RES-1, R-RES-2 | to write with L1 |
| `fuzz_png_walk` | PNG chunk walker (no decoder) | R-RES-3..5 | to write with the walker |
| `fuzz_two_pass` | `dui_sheets_load` and `dui_layout_load` with the buffer changed between the passes | R-GEN-11 | to write (section 4) |

A parser without a harness in this table does not pass S1.

## 3. What a harness must check

A harness that only calls the parser relies on the sanitizers noticing a defect by luck. Every harness aborts on a broken promise:

1. Ranges: every offset, size, span, index and pointer the parser returns lies inside the input or inside the array it indexes.
2. Counts: every count in the result is within its limit from `REQUIREMENTS.md`, and the statistics equal what an enumeration of the result finds.
3. Agreement: two ways to the same answer agree (`find` against `enum`; the validating pass against the filling pass).
4. Failure: a failed call leaves its output cleared (R-GEN-5).
5. Determinism: the same bytes parsed twice give the same status and the same model, compared field by field (R-GEN-8).
6. Copy independence: parsing a copy of the input placed at the end of an allocation of exactly its size gives the same result, so an over-read by one byte faults.

`fuzz_pe` and `fuzz_sheet` already do 1 to 4. Points 5 and 6 are added to all harnesses before the campaign.

## 4. Changing input (R-GEN-11)

A single-threaded fuzzer with a stable buffer cannot find a double fetch. `fuzz_two_pass` splits its input into two documents A and B of the same length, runs the loader on a buffer that holds A, and swaps in B from the between-passes hook. Any outcome but a clean reject or a model that is consistent with one of the two is an abort. The hooks exist only in builds with the test define, never in the shipped core.

`fuzz_pe` gets the same idea without a hook: open on A, overwrite the buffer with B, then run every lookup; the lookups must reject or stay in range.

## 5. Inputs

- Seeds are generated (R-PROC-1): `mkseeds` for PE images, small hand-written documents for the XML dialect. The L1 seeds come from the fixture builder that the L1 unit tests use.
- Dictionaries are generated from the sources: tag, attribute, property and function names, `MZ`, `PE\0\0`, resource type names, the five entity names. They are committed; they contain no file content.
- `fuzz_v4_style` uses a custom mutator that keeps the PE headers and the resource tree valid and mutates the resource bodies, so the campaign spends its time in the validator and not in header rejects. `fuzz_pe` stays unstructured.
- Phase B only: the reference corpus is given at run time through `TL4_CORPUS_DIR` as additional seeds in a temporary directory. Nothing from that directory, and no input derived from it, is committed. A finding on a corpus-derived input is reduced to a generated reproducer first.

## 6. Builds

| Build | Flags | Why |
|---|---|---|
| A | clang, `-fsanitize=fuzzer,address,undefined -fno-sanitize-recover=all`, 64-bit | memory safety, undefined behaviour |
| B | clang, `-fsanitize=fuzzer,memory -fsanitize-memory-track-origins` | uninitialised reads (R-GEN-8) |
| C | build A with `-m32` | `size_t` and pointer width; the offsets are checked in 64 bits and must not depend on it |
| D | `-fprofile-instr-generate -fcoverage-mapping`, no fuzzer, replays the final corpus | the coverage figures of the report |

Run options for all: `-timeout=5 -rss_limit_mb=1024 -malloc_limit_mb=256 -use_value_profile=1`, `-max_len` set to the limit of the target (512 KB for UIFILE, 4 MB for a PE image in phase A, the 32 MB limit in one dedicated run), one dictionary per target.

## 7. Budget and stop rule

Machine: `libernt-srv`, at most 8 of its 16 threads, niced, in one isolated directory that is deleted afterwards. Nothing is run there before the coordinator agrees to the time window.

- Phase A (generated seeds): each target runs on build A until it has had at least 16 CPU-hours and has found no new coverage for the last quarter of its time; then 4 CPU-hours each on builds B and C from the corpus of build A.
- Phase B (with the reference corpus as seeds): 8 CPU-hours per target on build A.
- A finding stops the target. It restarts from zero after the fix.

## 8. Exit criteria

1. No open crash, leak, timeout, out-of-memory or promise abort on any build.
2. Build D shows every `return` of an error status in the core reached by the final corpus, or the report names the line and says why it cannot be reached.
3. Every limit of `REQUIREMENTS.md` section 6 and 9.5 is hit at the limit (accepted) and over it (rejected) by a test or a corpus input.
4. Every finding has a regression test that cites the requirement it violated (R-PROC-2), and its reproducer is generated.

## 9. Report

`S1-REPORT.md` records, per target: the commit, the build commands, the run options, CPU-hours and executions, final corpus size, line and branch coverage from build D, the findings with requirement id, fix commit and test, and the unreached lines with their reason. It labels each statement as verified by a run, or as read only.
