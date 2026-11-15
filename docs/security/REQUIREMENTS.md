# Security requirements

This is the single normative list for Theme Loader 4. A milestone is not accepted until the security reviewer has checked its code against the requirements that apply to it. Change this file only by a commit that says why; a limit changes only with corpus evidence.

- **MUST** is mandatory. **SHOULD** needs a written reason in the commit when it is not followed.
- Each requirement has an id. Tests, fuzz findings and review notes cite the id.
- Origin: the internal theme-parser boundary review of 2026-11-15 (policy rules and the check of the limits table in `docs/design/V4-LOADER-SKETCH.md` section 3). Where this file and the sketch disagree, this file wins and the sketch is corrected.

## 1. Threat model

- A style file (`.msstyles`, its `.mui`, later `shellstyle.dll`) is **untrusted input**. Its author is assumed hostile. A signature, if present, is ignored by the parser.
- The loader runs in every GUI process of the user, including processes of that user with higher privilege (owner decision 2026-11-15: elevated processes use the user's style, as Windows does). So a defect here is a defect in all of them.
- Goals, in order: no memory-unsafe behaviour on any input; no unbounded time or memory on any input; a rejected style changes nothing.
- Not goals: confidentiality of the style, protection against the administrator, protection of the image decoders themselves (they are fenced by R-RES, not fixed here).

## 2. General rules (all code in `loader/` and `directui/`)

| Id | Requirement |
|---|---|
| R-GEN-1 | MUST: the core takes `(const uint8_t *data, size_t size)` and reads nothing else. No file, registry, environment or OS call in the core. |
| R-GEN-2 | MUST: every read of input goes through bounded little-endian helpers that take an offset and fail when `offset + width > size`. No struct is cast over input bytes. No pointer derived from a file-supplied value without the bounds check. |
| R-GEN-3 | MUST: arithmetic on file-supplied values is done in 64 bits or written as `x <= limit - y` with `y <= limit` already known. This covers every sum, product and alignment (`off + 32`, `off + cbData`, `ALIGN8(off)`, `4 + 4*count`, `width * height * 4`, `PointerToRawData + SizeOfRawData`, `OffsetToData + Size`). No division or modulo by a file-supplied value without a zero check. |
| R-GEN-4 | MUST: every multi-byte integer field read from the input is treated as unsigned, then range-checked. A field documented as `int32` with its top bit set is a reject. |
| R-GEN-5 | MUST: reject, never truncate, clamp or repair input. One error code per failure; a failed call returns no partial result. |
| R-GEN-6 | MUST: no recursion on input structure and no loop whose bound comes only from the input. Every loop has a limit from section 6. |
| R-GEN-7 | MUST: validation allocates nothing and needs no scratch memory that grows with the input. Allocation happens only after the whole input has passed, and its total is bounded by the counts the validator returned. |
| R-GEN-8 | MUST: results are deterministic. The same bytes give the same accept/reject and the same model on every host and in both callers (install and load). No dependence on locale, time or uninitialised memory. |
| R-GEN-9 | MUST: the core builds warning-free with `-Wall -Wextra -Wconversion` (and `/W4` on MSVC) and runs its tests under AddressSanitizer and UndefinedBehaviorSanitizer in CI. |
| R-GEN-10 | MUST: one validator. Install-time checking and load-time checking call the same function with the same limits. |

## 3. Bounded PE/resource reader (milestone L0)

| Id | Requirement |
|---|---|
| R-PE-1 | MUST: the system resource functions (`FindResource`, `SizeofResource`, `LoadResource`, `LoadString`, `LoadImage`) are not used on a style. The reader below is the only way to reach resource bytes. |
| R-PE-2 | MUST: check the DOS header (`size >= 64`, `MZ`), then `e_lfanew` against the size of the NT headers, then the `PE\0\0` signature. Accept PE32 and PE32+ only. |
| R-PE-3 | MUST: `NumberOfSections` is 1..32. `SizeOfOptionalHeader` covers the data directory up to the resource entry. The section table lies inside the file. |
| R-PE-4 | MUST: for every section, `PointerToRawData + SizeOfRawData <= size`. An RVA is translated only through a section whose raw data contains it; an RVA beyond a section's raw data is a reject, not zero fill. |
| R-PE-5 | MUST: the resource directory (data directory entry 2) lies wholly inside one section's raw data. Every directory header, entry array, name string and data entry is checked to lie inside that range before it is read. |
| R-PE-6 | MUST: the tree is exactly three levels (type, name, language). A subdirectory bit at level three, or a data entry above level three, is a reject. The walk is iterative. |
| R-PE-7 | MUST: per directory, named + id entries <= 16,384; total entries visited per file <= 65,536. Name strings are length-prefixed UTF-16, at most 64 characters, compared without locale. |
| R-PE-8 | MUST: the entries of every directory are in strictly ascending order (ids by value, names by UTF-16 code unit, names before ids), as the PE format requires; any other order is a reject. This makes duplicates impossible and lookups unambiguous. A (type, name) pair has exactly one language entry; more than one is a reject. |
| R-PE-9 | MUST: a data entry's `OffsetToData` is an RVA translated by R-PE-4, and `[offset, offset + Size)` lies inside the file. The reader returns `(offset, size)`, never a pointer computed from unchecked values. |
| R-PE-10 | MUST: nothing else in the file is parsed: no imports, exports, relocations, debug data, certificate table or overlay. |
| R-PE-11 | MUST: a libFuzzer harness for the reader exists in the same commit as the reader, builds with ASan + UBSan, and runs in CI on a seed set of generated fixtures. |

## 4. Version-4 container validator (milestone L1)

| Id | Requirement |
|---|---|
| R-V4-1 | MUST: `PACKTHEM_VERSION` is exactly 2 bytes with value 4. Other values are not this loader's input. |
| R-V4-2 | MUST: `CMAP` has even size; every name is NUL-terminated inside the resource; a class name has at most one `::`, both halves are non-empty and at most 59 characters; duplicate names are a reject. The class id is the ordinal of the n-th non-empty name. |
| R-V4-3 | MUST: `VMAP` is three length-prefixed strings, each inside the resource and at most 64 characters. The sketch states whether the prefix counts bytes or characters before L1 is written. |
| R-V4-4 | MUST: record walk over `VARIANT` and `RMAP`: 32-byte headers, 8-byte alignment, and the walk ends exactly at the resource size (or exactly at `ALIGN8(size)` if the corpus shows unaligned sizes; the sketch states which). Leftover bytes that are not a whole record are a reject. |
| R-V4-5 | MUST: header fields: `nameId` 1..65,535; `typeId` 200..255; `classId < CMAP count`; `partId` and `stateId` 0..255; `reserved == 0`; `cbData <= 65,536` and inside the resource. Any field outside its range is a reject. |
| R-V4-6 | MUST: for a known `typeId` with `reference == 0`, `cbData` is exact: INT, BOOL, ENUM, COLOR, SIZE 4; POSITION 8; RECT, MARGINS 16; INTLIST `4 + 4*count` with `count <= 402` (the caller's `INTLIST` capacity); STRING even, at most 4,096 bytes, last character NUL. |
| R-V4-7 | MUST: `reference` is an identifier and nothing else. For FONT it names a string, for FILENAME an `IMAGE`, for DISKSTREAM a `STREAM`, and it must resolve through the reader. It is never used as an offset, index, pointer or length, and the `cbData` of a record with `reference != 0` is never used as a length. |
| R-V4-8 | MUST: a record of an unknown `typeId`, or of a type that carries no reference with `reference != 0`, is skipped by its checked `cbData`, dropped and counted. It never aborts the walk and never reaches the model. |
| R-V4-9 | MUST: the validator does not look for duplicate (class, part, state, nameId) records (R-GEN-7). The model builder does: the first record wins, later ones are dropped and counted (R-MOD-1 still holds). |
| R-V4-10 | MUST: `BCMAP` is not interpreted until its meaning is VERIFIED in the sketch. Once it is used for the override chain: entry count equals the CMAP count, each entry is a valid class id or the no-base value, and the chain from every class is acyclic with depth at most 8. A cycle or a longer chain is a reject. |
| R-V4-11 | MUST: resources this loader does not use (`AMAP`, `PVL`, `IMMERSIVE`, `DESKTOP`, anything unknown) are not parsed. They count toward the file limits only. |
| R-V4-12 | MUST: the validator returns the counts (classes, part-states, properties, dropped records by reason). More than 1% of records dropped for an unresolved reference, or any unresolved FONT reference, is a reject. |
| R-V4-13 | MUST: the corpus runner shows that all reference styles validate under these limits and reports the maxima. A limit that a reference style exceeds is raised here by commit, not in code. |

## 5. Property model, strings, images (milestones L2, L3)

| Id | Requirement |
|---|---|
| R-MOD-1 | MUST: building the model does not search a list per record. Cost is O(records) or O(records log records) at the limits of section 6. |
| R-MOD-2 | MUST: a getter returns only values whose type and size the validator checked. No getter re-parses input bytes or trusts a stored length it did not get from the validator. |
| R-MOD-3 | MUST: every size, margin, count and font height is clamped to a documented range before it leaves the loader toward a system setting or is used as a divisor. |
| R-RES-1 | MUST: strings are read with the bounded reader from the string table (16 length-prefixed strings per block, each length checked against the block). No MUI redirection. A `.mui` is opened explicitly and is subject to every requirement that applies to the style. |
| R-RES-2 | MUST: only string ids referenced by accepted records are read. A font string is at most 255 characters; its face name is at most 31 characters. |
| R-RES-3 | MUST: before an image reaches any decoder, a chunk walker checks: PNG signature; IHDR first, with bit depth 8, colour type 2 or 6, no interlace (widened only with corpus evidence); every chunk length inside the resource; every CRC; IEND last; no chunk type other than IHDR, PLTE, tRNS, IDAT, IEND. |
| R-RES-4 | MUST: image limits from section 6 are checked from IHDR before decoding, and decoded pixels are counted against the per-style budget for the life of the process. |
| R-RES-5 | MUST: `IMAGE` and `STREAM` resources are PNG only. No BMP, no other format, no format sniffing by the decoder. |

## 6. Limits

Corpus maxima are from the sketch (32 reference styles). A value here is normative; the sketch's table is corrected to match.

| Quantity | Corpus maximum | Limit |
|---|---|---|
| File size | 2,976,256 bytes | 32 MB |
| Sections | n/a | 32 |
| Resource directory entries, per directory / per file | n/a | 16,384 / 65,536 |
| `CMAP` size / names | 18,760 bytes / 374 | 256 KB / 1,024 |
| `VARIANT` size / records | 449,072 bytes / 11,096 | 2 MB / 32,768 |
| `RMAP` size | about 0.5 KB | 64 KB |
| Classes / part-states / properties in the model | 374 / about 2,000 / 11,096 | 1,024 / 8,192 / 32,768 |
| `cbData` inline | 1,608 | 65,536, and exact per type (R-V4-6) |
| INTLIST count | 401 | 402 |
| `IMAGE` / `STREAM` resources | 1,611 / 3 | 4,096 / 16 |
| One image: compressed size / side / pixels | n/a / 3,720 / about 5 M | 4 MB / 8,192 / 8 M |
| Decoded pixels per style, per process | 6.48 M | 16 M |
| Override chain depth | 2 | 8 |

## 7. Policy for the integration milestones (recorded here, enforced outside the core)

| Id | Requirement |
|---|---|
| R-POL-1 | MUST: the active style lives in a system-owned store on a local fixed volume, reached without reparse points, not writable by ordinary users. Installing is an administrator action; choosing among installed styles is a user action. |
| R-POL-2 | MUST: a style is never loaded as code. It is mapped read-only as data, and the size checked is the size of the mapping, taken from the same handle. |
| R-POL-3 | MUST: a style is validated at install in a low-privilege throwaway process, and again at every load (R-GEN-10). A style that fails is never offered and never written to the user's settings. |
| R-POL-4 | MUST: fail closed. Any error leaves the process on the unthemed look, with no partial theme. |
| R-POL-5 | MUST: crash guard. A mark is set before a style is loaded and cleared after; a process that finds the mark at start-up skips the style. |
| R-POL-6 | MUST: paths inside a `.theme` file are untrusted and resolve only inside the store. Companion binaries shipped with theme packages are code and are never installed by a theme installer. |

## 8. Process

| Id | Requirement |
|---|---|
| R-PROC-1 | MUST: no Microsoft or third-party theme asset, and nothing extracted from one, enters the repository, a fixture, a fuzz seed or a crash artefact that is committed. Seeds are generated. A crashing input found on the reference corpus is reduced to a generated reproducer before it is committed. |
| R-PROC-2 | MUST: every fuzz finding gets a regression test that cites the requirement id it violated. |
| R-PROC-3 | MUST: L1 does not start before the L0 review is closed, L2 not before the S1 campaign report, integration not before S2. |
| R-PROC-4 | SHOULD: each requirement in sections 3 to 5 has at least one negative test (an input that must be rejected for that reason alone). |

## 9. Open points

- R-V4-3, R-V4-4: two format facts the sketch must state (VMAP prefix unit, walk end on unaligned sizes).
- R-V4-10: `BCMAP` semantics (sketch open item 1).
- R-RES-3: the allowed IHDR set must be confirmed on the corpus (colour types and interlace actually used).
- R-PE-8: strict ordering must be confirmed on the corpus by the L0 corpus run; if a reference style is unordered, the rule becomes "linear scan, first match" by commit here.
- Record types 240, 241, 243 and the records with a non-zero reference on value types are dropped by R-V4-8 until the sketch explains them.
