# Security requirements

This is the single normative list for Theme Loader 4. A milestone is not accepted until the security reviewer has checked its code against the requirements that apply to it. Change this file only by a commit that says why; a limit changes only with corpus evidence.

- **MUST** is mandatory. **SHOULD** needs a written reason in the commit when it is not followed.
- Each requirement has an id. Tests, fuzz findings and review notes cite the id.
- Origin: the internal theme-parser boundary review of 2026-11-15 (policy rules and the check of the limits table in `docs/design/V4-LOADER-SKETCH.md` section 3). Where this file and the sketch disagree, this file wins and the sketch is corrected.

## 1. Threat model

- A style file (`.msstyles`, its `.mui`, later `shellstyle.dll`) is **untrusted input**. Its author is assumed hostile. A signature, if present, is ignored by the parser.
- The loader runs in every GUI process of the user, including processes of that user with higher privilege. Owner decisions of 2026-11-15: elevated processes follow the user's choice of style, as Windows does, but a privileged process loads theme resources only from a protected location that unprivileged users cannot write (R-POL-7). So a defect here is a defect in all of them, and the policy limits who can reach it.
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
| R-GEN-11 | MUST: the input may change while it is parsed (a shared mapping of a file the user can write). No decision relies on two reads of the same input byte giving the same value. A value used for a bound is read once into a local. A second pass over the input checks every write against the sizes that were allocated from the first pass, before the write, or both passes run on a private copy made before the first. |

## 3. Bounded PE/resource reader (milestone L0)

| Id | Requirement |
|---|---|
| R-PE-1 | MUST: the system resource functions (`FindResource`, `SizeofResource`, `LoadResource`, `LoadString`, `LoadImage`) are not used on a style. The reader below is the only way to reach resource bytes. |
| R-PE-2 | MUST: check the DOS header (`size >= 64`, `MZ`), then `e_lfanew` against the size of the NT headers, then the `PE\0\0` signature. Accept PE32 and PE32+ only. |
| R-PE-3 | MUST: `NumberOfSections` is 1..32. `SizeOfOptionalHeader` covers the data directory up to the resource entry. The section table lies inside the file. |
| R-PE-4 | MUST: for every section, `PointerToRawData + SizeOfRawData <= size`. An RVA is translated only through a section whose raw data contains it; an RVA beyond a section's raw data is a reject, not zero fill. |
| R-PE-5 | MUST: the resource directory (data directory entry 2) lies wholly inside one section's raw data. Every directory header, entry array, name string and data entry is checked to lie inside that range before it is read. |
| R-PE-6 | MUST: the tree is exactly three levels (type, name, language). A subdirectory bit at level three, or a data entry above level three, is a reject. The walk is iterative. |
| R-PE-7 | MUST: per directory, named + id entries <= 4,096; total entries visited per file <= 65,536; data entries per file <= 16,384. Name strings are length-prefixed UTF-16, at most 64 characters, compared without locale. |
| R-PE-8 | MUST: the entries of every directory are in strictly ascending order (ids by value, names by UTF-16 code unit, names before ids), as the PE format requires; any other order is a reject. This makes duplicates impossible and lookups unambiguous. A (type, name) pair has at least one and at most 64 language entries, all of them ids (a named entry at level three is a reject), counted in the totals of R-PE-7; every one is checked under R-PE-9. The reader records whether any pair in the file has more than one. A lookup by type and name returns the entry with the lowest language id, and reports how many the pair has. Two users of the reader stay strict: the version-4 validator rejects a file in which any pair has more than one language entry (so the set of files the version-4 path accepts is unchanged), and the resource that routes a file to a loader (`PACKTHEM_VERSION`) must have exactly one language entry, or the file is refused for both loaders. Reason for the change (2026-11-15): a localised version-3 style carries one string table per language (the shipped style has 14), and the bounded reader must be able to read its version before the version-3 loader is allowed near it (trust policy T-LOAD-2, T-GUARD-1). |
| R-PE-9 | MUST: a data entry's `OffsetToData` is an RVA inside the raw data of the section that holds the resource directory (R-PE-5), and `[offset, offset + Size)` lies inside that raw data; data in any other section is a reject. The reader returns `(offset, size)`, never a pointer computed from unchecked values. |
| R-PE-10 | MUST: nothing else in the file is parsed: no imports, exports, relocations, debug data, certificate table or overlay. |
| R-PE-11 | MUST: a libFuzzer harness for the reader exists in the same commit as the reader, builds with ASan + UBSan, and runs in CI on a seed set of generated fixtures. |

## 4. Version-4 container validator (milestone L1)

| Id | Requirement |
|---|---|
| R-V4-1 | MUST: `PACKTHEM_VERSION` is exactly 2 bytes with value 4. Other values are not this loader's input. |
| R-V4-2 | MUST: `CMAP` has even size; every name is NUL-terminated inside the resource; a class name has at most one `::`, both halves are non-empty and at most 59 characters. The class id is the ordinal of the n-th non-empty name. A name may occur more than once (three reference styles of one third-party family repeat `TaskbandExtendedUI`; they are published as working Windows styles, which we have not tested): records and `BCMAP` address classes by ordinal, so every ordinal stays valid, and a lookup by name returns the lowest ordinal with that name. The validator does not search for duplicates (R-V4-14); the model builder counts them when it builds its name index. |
| R-V4-3 | MUST: `VMAP` is three length-prefixed strings, each inside the resource and at most 64 characters. The sketch states whether the prefix counts bytes or characters before L1 is written. |
| R-V4-4 | MUST: record walk over `VARIANT` and `RMAP`: 32-byte headers, 8-byte alignment, and the walk ends exactly at the resource size (or exactly at `ALIGN8(size)` if the corpus shows unaligned sizes; the sketch states which). Leftover bytes that are not a whole record are a reject. |
| R-V4-5 | MUST: header fields: `nameId` 1..65,535; `typeId` 200..255; `classId < CMAP count`; `partId` and `stateId` 0..255; the seventh field is not interpreted and never read into a decision (it is 0 in every record of the two stock styles; 20 of the 30 third-party styles carry other values there in about 2,450 reference and 100 inline records, corpus run 2026-11-15); `cbData <= 65,536` and inside the resource. Any field outside its range is a reject. |
| R-V4-6 | MUST: for a known `typeId` with `reference == 0`, `cbData` is exact: INT, BOOL, ENUM, COLOR, SIZE 4; POSITION 8; RECT, MARGINS 16; INTLIST `4 + 4*count` with `count <= 402` (the caller's `INTLIST` capacity); STRING even, at most 4,096 bytes, last character NUL. |
| R-V4-7 | MUST: `reference` is an identifier and nothing else. For FONT it names a string, for FILENAME an `IMAGE`, for DISKSTREAM a `STREAM`, for STRING (when non-zero) a string, and it must resolve through the reader (every reference style's `RMAP` carries its documentation strings 601-608 as STRING records referencing string ids 101-112; corpus run 2026-11-15). It is never used as an offset, index, pointer or length, and the `cbData` of a record with `reference != 0` is never used as a length. |
| R-V4-8 | MUST: a record of an unknown `typeId`, or of a type that carries no reference with `reference != 0`, is skipped by its checked `cbData`, dropped and counted. It never aborts the walk and never reaches the model. |
| R-V4-9 | MUST: the validator does not look for duplicate (class, part, state, nameId) records (R-GEN-7). The model builder does: the first record wins, later ones are dropped and counted (R-MOD-1 still holds). |
| R-V4-10 | MUST: `BCMAP` is not interpreted until its meaning is VERIFIED in the sketch. Once it is used for the override chain: entry count equals the CMAP count, each entry is a valid class id or the no-base value, and the chain from every class is acyclic with depth at most 8. A cycle or a longer chain is a reject. |
| R-V4-11 | MUST: resources this loader does not use (`AMAP`, `PVL`, `IMMERSIVE`, `DESKTOP`, anything unknown) are not parsed. They count toward the file limits only. |
| R-V4-12 | MUST: the validator returns the counts (classes, part-states, properties, dropped records by reason). More than 1% of records dropped for an unresolved reference, or any unresolved FONT reference, is a reject. |
| R-V4-13 | MUST: the corpus runner shows that all reference styles validate under these limits and reports the maxima. A limit that a reference style exceeds is raised here by commit, not in code. |
| R-V4-14 | MUST: validation cost is bounded at the limits of section 6. No step re-walks a map or a resource directory once per item: a reference is resolved by a search that uses the strict order of R-PE-8 (logarithmic in the directory), not by a linear walk per record, and no check compares every pair of names by re-reading the map from its start. A generated style at the limits (32,768 records that each carry a reference, 4,096 images, a 256 KB `CMAP`) validates inside the fuzz timeout of the S1 plan, and a test shows it. |

## 5. Property model, strings, images (milestones L2, L3)

| Id | Requirement |
|---|---|
| R-MOD-1 | MUST: building the model does not search a list per record. Cost is O(records) or O(records log records) at the limits of section 6. |
| R-MOD-2 | MUST: a getter returns only values whose type and size the validator checked. No getter re-parses input bytes or trusts a stored length it did not get from the validator. |
| R-MOD-3 | MUST: every size, margin, count and font height is clamped to a documented range before it leaves the loader toward a system setting or is used as a divisor. |
| R-MOD-4 | MUST: the model holds no pointer into the input. A value is an offset and a size inside a record area whose file range the build fixed and checked; the build stores type and size in the model, and a getter takes both from the model, never from the input again. A getter copies the value bytes once, with the range checked against the mapping size, and every later check (a list count, a final NUL, a font string) runs on the copy (R-GEN-11). A record that carries no inline data has size 0 in the model. |
| R-MOD-5 | MUST: the core allocates nothing. The caller provides one buffer; the size comes from the validator's statistics with checked arithmetic, and the build refuses a buffer that is too small or misaligned. The build trusts the statistics for sizing only: it runs the class map, base map and record checks of section 4 again on the bytes it reads, uses only the results of that run, and fails, without a partial model, as soon as any array would pass the size the buffer was made for (R-GEN-5, R-GEN-7, R-GEN-11). After a successful build the model is never written; queries are read-only and may run on several threads. |
| R-MOD-6 | MUST: a lookup is a fixed number of binary searches. It visits the class, at most 8 base classes (R-V4-10) and at most one final class `globals`, which the build resolves once; the walk counts its steps and stops at that bound whatever the model holds. The search order (state, then part, then class, each along the chain, then `globals`) is the behaviour of the current engine, recorded as such: a change needs a measurement on Windows and a commit here. Records of the two record areas are kept apart: a `VARIANT` record never answers a documentation query, and an `RMAP` record never answers a class query. |
| R-MOD-7 | MUST: the getters of the core return the values of the file and apply no clamp, so the corpus dump stays the test oracle. The range checks of R-MOD-3 are functions of the loader library, host-tested and fuzzed with it, driven by one table, section 6.1 of this document. A consumer (the uxtheme adapter, the DirectUI theme queries of R-DUI-15) calls these functions and has no range table of its own. A consumer never passes a raw getter result to a system setting, a divisor, an index or an allocation size; a value outside its range is a reject of that property, not a repaired value (R-GEN-5). The core itself never uses a model value as a divisor, an index or an allocation size without its own check. |
| R-RES-1 | MUST: strings are read with the bounded reader from the string table (resource type 6; block `b` holds the ids `16(b-1)` to `16b-1` as 16 length-prefixed strings, each length checked against the block). No MUI redirection and no language fallback: the core never selects, locates or opens a file. The consumer may open at most one strings module (`.mui`) per style. Its path is the one of T-STORE-3, with a language name that satisfies T-STORE-4 and that the consumer decides once; it is opened, checked and mapped under the same rules as the style (R-POL-1, R-POL-2, T-LOAD) and is subject to every requirement that applies to the style, at install and at load (R-GEN-10). A string is taken from the table of the style when that table has it (the block is present and the slot is not empty), otherwise from the module. Only "not found" in the style leads to the module; any other failure is returned as it is. |
| R-RES-2 | MUST: every block of a string table is shape-checked (R-RES-8), but the text of a string is read only for an id that an accepted record references or that the consumer asks for by id. A string that a record references is at most 255 characters. A font string is at most 255 characters; its face name is at most 31 characters. |
| R-RES-3 | MUST: before an image reaches any decoder, a chunk walker checks, in one pass: the PNG signature; every chunk length inside the resource; the CRC of every chunk; IHDR first, 13 bytes, with bit depth 8, colour type 2 or 6, compression 0, filter 0, no interlace; one or more IDAT chunks, contiguous; IEND last, of length 0, and the resource ends exactly there. The only other chunk types allowed are `gAMA` (4 bytes, value not 0), `pHYs` (9 bytes, unit 0 or 1), `sRGB` (1 byte, value 0..3) and `tEXt` (at most 256 bytes: a keyword of 1..79 bytes, a NUL, then text; every byte except that NUL is printable Latin-1, 32..126 or 161..255), each at most once and before the first IDAT. The walker bounds these four and uses their values for nothing else. Every other chunk type is a reject, never skipped; this includes `PLTE` and `tRNS`, which no reference image carries. The set is widened only with corpus evidence and a commit here. |
| R-RES-4 | MUST: image limits from section 6 are checked from IHDR before decoding, and decoded pixels are counted against the per-style budget for the life of the process. |
| R-RES-5 | MUST: `IMAGE` and `STREAM` resources are PNG only. No BMP, no other format, no format sniffing by the decoder. |
| R-RES-6 | MUST: a decoder receives only a private copy of the resource, of exactly the checked length, that the walker checked after the copy was taken; the copy is not changed between the check and the decoding. The size limit of section 6 is applied before the copy is made. A result of the walker is never applied to bytes it did not read (R-GEN-11). After decoding, the consumer compares the decoder's width and height with the walker's and rejects the image when they differ. This holds for every consumer of a style image, DirectUI included (R-DUI-14). |
| R-RES-7 | MUST: an image has at most 64 chunks and at most 64 IDAT chunks (section 6); the walker stops at these counts whatever the lengths say. |
| R-RES-8 | MUST: at validation, every language entry (R-PE-8) of every block of the string table of the style and of the strings module passes the shape check: the block name is an id 1..4,096 (a named entry is a reject); the data is at most 32,800 bytes and is exactly 16 strings, each a 16-bit count followed by that many UTF-16 code units, each count at most 1,024, the last string ending exactly on the size of the block; no string contains the code unit 0. One block that fails rejects the whole file. A lookup is one bounded search and one pass over at most 16 counts; it does not rely on the earlier check: it bounds the block and every count again, and uses each count from one read (R-GEN-11). The getter copies the string once into the buffer of the caller, applies the checks to the copy (length, no code unit 0), ends it with one 0, and fails without output when the buffer is too small (R-GEN-5); on any failure the output is cleared. A lookup names a language id (0..65,535) or asks for the lowest id of the block; any other language value is an argument error; a language that the block does not have is "not found". The core applies no fallback between languages. |

## 6. Limits

Corpus maxima are from the sketch (32 reference styles). A value here is normative; the sketch's table is corrected to match.

| Quantity | Corpus maximum | Limit |
|---|---|---|
| File size | 2,976,256 bytes | 32 MB |
| Sections | n/a | 32 |
| Resource directory entries, per directory / per file | 1,611 in one directory | 4,096 / 65,536 |
| Resource data entries per file | about 1,630 | 16,384 |
| `CMAP` size / names | 18,760 bytes / 374 | 256 KB / 1,024 |
| `VARIANT` size / records | 449,072 bytes / 11,096 | 2 MB / 32,768 |
| `RMAP` size | about 0.5 KB | 64 KB |
| Classes / part-states / properties in the model | 374 / about 2,000 / 11,096 | 1,024 / 8,192 / 32,768 |
| `cbData` inline | 1,608 | 65,536, and exact per type (R-V4-6) |
| INTLIST count | 401 | 402 |
| `IMAGE` / `STREAM` resources | 1,611 / 3 | 4,096 / 16 |
| One image: compressed size (IDAT total) / side / pixels | 345,332 bytes / 3,720 / 1,237,800 | 4 MB / 8,192 / 8 M |
| One image: resource size / chunks / IDAT chunks | 345,499 bytes / 12 / 6 | 4 MB + 64 KB / 64 / 64 |
| One image: `tEXt` size | 27 bytes | 256 bytes |
| String table: blocks per file / block size / string length / languages per block | 61 / 668 bytes / 72 characters / 14 (the shipped version-3 style; 1 in every version-4 file) | 4,096 / 32,800 bytes / 1,024 characters (255 when a record references it) / 64 (R-PE-8) |
| Decoded pixels per style, per process | 6.48 M | 16 M |
| Override chain depth | 2 | 8 |

### 6.1 Value ranges (R-MOD-3, R-MOD-7)

One table, one implementation in `loader/`. The range function takes the type and the property id of the model record and the value the getter returned, and answers "in range" or "out of range"; it changes no value. Out of range is a reject of that property: the consumer reports it as unsupported, and where the engine has a default for a missing property, the rejected property behaves as missing. A property row wins over the type row. Property names are those of `vssym32.h`; the implementation lists their numbers and a test compares the list with this table.

| Type or property | Range | Why |
|---|---|---|
| `INT`, `SIZE`; each component of `POSITION`, `RECT`, `MARGINS`; each entry of an `INTLIST` | -8,192 .. 8,192 | a value of these types can become a pixel length or an offset. 8,192 is the largest image side (section 6), so a product of two values and of a value with 4 bytes per pixel stays inside 32 bits. Corpus maxima are below 4,000 |
| `TMT_IMAGECOUNT` and every other count that the consumer uses as a divisor of an image size | 1 .. 256 | 0 and negative values would divide by zero or turn the strip around |
| `TMT_GLYPHINDEX` and every other value that the consumer uses as an index | 0 .. 255 | the index is checked again against the object it indexes |
| `TMT_ALPHALEVEL`, `TMT_ALPHATHRESHOLD`, `TMT_OPACITY` and every other value used as an 8-bit level | 0 .. 255 | |
| durations and delays in milliseconds (`TMT_ANIMATIONDURATION`, `TMT_ANIMATIONDELAY`, the entries of `TMT_TRANSITIONDURATIONS`, and every other time value) | 0 .. 65,535 | no pixel use; bounded so that later arithmetic cannot overflow |
| `TMT_MINDPI1` .. `TMT_MINDPI7` and every other DPI value | 1 .. 8,192 | used in a scale division |
| `BOOL` | 0 or 1 | the getter gives 0 or 1 and nothing else |
| `ENUM` | 0 .. 255, and the consumer accepts only the values it knows for that property | an unknown value is unsupported, never a default drawing mode |
| `COLOR` | any 32-bit value | a colour is never used in arithmetic that can fail |
| `INTLIST` count | 0 .. 402 | R-V4-6 |
| `FONT` | face 1 .. 31 characters; size 1 .. 255 points | R-RES-2, `LF_FACESIZE` |
| `STRING` | 1 .. 255 characters when a record references it, no code unit 0 | R-RES-2, R-RES-8 |
| `FILENAME`, `DISKSTREAM`, `STREAM`, `BITMAPREF` | no value range: the reference is resolved by the validator, and the image limits of this section apply before decoding | R-V4-12, R-RES-3, R-RES-4 |
| `HBITMAP`, `FLOAT`, `FLOATLIST` | always out of range | no getter serves these types; a consumer never reads their bytes |
| system metric sizes taken from a style (border, scroll bar, caption and menu sizes) | 0 .. 512 | they go to a system setting; the corpus uses 1 .. 40 |

A property that is in more than one row (a count that is also an `INT`) takes the narrower row. The words "every other" are closed by the implementation: its list of property numbers per row is reviewed with the code, and a property that is used as a divisor, an index, a level, a time or a DPI value and is missing from its row is a defect.

## 7. Policy for the integration milestones (recorded here, enforced outside the core)

| Id | Requirement |
|---|---|
| R-POL-1 | MUST: the active style lives in a system-owned store on a local fixed volume, reached without reparse points, not writable by ordinary users. Installing is an administrator action; choosing among installed styles is a user action. |
| R-POL-2 | MUST: a style is never loaded as code. It is mapped read-only as data, and the size checked is the size of the mapping, taken from the same handle. |
| R-POL-3 | MUST: a style is validated at install in a low-privilege throwaway process, and again at every load (R-GEN-10). A style that fails is never offered and never written to the user's settings. |
| R-POL-4 | MUST: fail closed. Any error leaves the process on the unthemed look, with no partial theme. |
| R-POL-5 | MUST: crash guard. A mark is set before a style is loaded and cleared after; a process that finds the mark at start-up skips the style. |
| R-POL-6 | MUST: paths inside a `.theme` file are untrusted and resolve only inside the store. Companion binaries shipped with theme packages are code and are never installed by a theme installer. |
| R-POL-7 | MUST: R-POL-1 to R-POL-6 are enforced as written in `docs/security/THEME-TRUST-POLICY.md` (rules T-DEF, T-STORE, T-LOAD, T-PRIV, T-INST, T-SIG, T-FAIL, T-GUARD, T-GATE). That file defines what a user-writable location and a privileged process are, the protected store and its ACL, the installer's validation, the origin mark, and the gate before the version-4 path is enabled. The rules bind every loader in service, the version-3 one included. The integration design (I1) cites those ids; enforcement is reviewed by the security reviewer before it is enabled. |

## 8. Process

| Id | Requirement |
|---|---|
| R-PROC-1 | MUST: no Microsoft or third-party theme asset, and nothing extracted from one, enters the repository, a fixture, a fuzz seed or a crash artefact that is committed. Seeds are generated. A crashing input found on the reference corpus is reduced to a generated reproducer before it is committed. |
| R-PROC-2 | MUST: every fuzz finding gets a regression test that cites the requirement id it violated. |
| R-PROC-3 | MUST: L1 does not start before the L0 review is closed, and each later milestone not before the review of the one before it is closed. Security validation of the parsers is the bounded check of `docs/security/BOUNDED-VALIDATION.md` (owner decision 2026-11-15: it replaces the fuzz campaigns S1 and S2; a check has an objective, a budget and a stop rule, and run counts are not a result). Reading for I1 (owner decision 2026-11-15: design and initial implementation may start now, validation before production use): after the I1 design review is closed, I1 code may be merged compiled out by default; the build option is turned on only in lab builds; the version-4 path is switched on outside the lab only when the bounded check passed on the release candidate that is vendored, the table of section 6.1 is in this file and T-GATE-1 of the trust policy is met. For DirectUI, see R-DUI-18. |
| R-PROC-4 | SHOULD: each requirement in sections 3 to 5 has at least one negative test (an input that must be rejected for that reason alone). |

## 9. DirectUI: UIFILE parser and style sheets (milestone D1)

Sections 1, 2 and 8 apply to `directui/` as they stand. Corpus figures are from `directui/docs/D0-RESEARCH.md` (127 UIFILE resources in 21 reference files).

### 9.1 What a theme may supply

| Id | Requirement |
|---|---|
| R-DUI-1 | MUST: from the `shellstyle.dll` of a style, the only DirectUI input is the resource of type `UIFILE` with id 1. Resources of any other type (`SHELL32_UIFILE`, `EXPLORERFRAME_UIFILE`, `SYSTEMCPL_UIFILE`, `USERCPL_UIFILE`, anything else) are never read from a style. |
| R-DUI-2 | MUST: a theme supplies style sheets only. The accepted document is `<duixml>` with `<stylesheets>` and `<style resid>` children; an element tree, `macro`, `bind` or any layout content in theme input is a reject. The entry point that loads theme sheets has no path that returns an element tree. Text that is not white space is accepted in one place only: directly inside `<style>` or `<if>`, between rules, where it is ignored and counted (every stock sheet has a stray `\` there). Text inside a selector element, or directly inside `<duixml>` or `<stylesheets>`, is a reject. |
| R-DUI-3 | MUST: a theme sheet may set visual properties only. The allow-list is fixed and exact, as measured on the stock sheets: `background`, `foreground`, `font`, `fontstyle`, `fontsize`, `fontweight`, `fontface`, `padding`, `margin`, `pressedpaddingshift`, `contentalign`, `height`, `width`, `minsize`, `borderthickness`, `bordercolor`, `borderstyle`, `transparent`. An assignment to any other property is dropped and counted by reason, never kept: text and accessibility (`content`, `tooltip`, `shortcut`, the `acc*` properties), identity and structure (`id`, `class`, `layout`, `layoutpos`, `sheet`), behaviour (`enabled`, `visible`, `cursor`), and everything unlisted, which includes `overhang`, `themesubapp` and the Explorer pane-sizer properties (`initialsize`, `reservedspace`, `explorervisibility`, `initialvisibility` and the like). A theme must not be able to relabel, hide, resize panes of or re-wire trusted UI. A name is added to the list only by a commit here, with the corpus evidence and the reason it is visual. |
| R-DUI-3a | MUST: the size-bearing properties on the list (`height`, `width`, `minsize`, `padding`, `margin`, `borderthickness`, `pressedpaddingshift`, `fontsize`) take only non-negative lengths within R-DUI-12 (`pressedpaddingshift` may be negative within the same range); the integration clamps them again before layout (R-MOD-3). Recorded residual risk: with these properties a theme can still shrink an element to nothing or make text unreadable, as on Windows; an integration that shows security-relevant UI does not take its sizes from a theme sheet. |
| R-DUI-4 | MUST: layout parsing exists for resources owned by the consuming binary and for tests. It is a separate entry point, and no integration may feed it bytes that come from a style. |

### 9.2 Input and XML subset

| Id | Requirement |
|---|---|
| R-DUI-5 | MUST: the core takes `(const uint8_t *data, size_t size)` of one resource (R-GEN-1). It never opens a module. The caller obtains the bytes through a reader that meets section 3; `directui/` does not link or include `loader/`. |
| R-DUI-6 | MUST: the input is not NUL-terminated and a NUL byte in it is a reject. Bytes below 0x80 are ASCII; bytes from 0x80 must form valid UTF-8, otherwise reject. |
| R-DUI-7 | MUST: accepted XML is elements, attributes, comments and text. A DOCTYPE, an entity declaration, a processing instruction (including the XML declaration), a CDATA section, or an entity reference other than the five predefined ones (`lt`, `gt`, `amp`, `quot`, `apos`) is a reject. No external resource is ever fetched. |
| R-DUI-8 | MUST: the tokenizer and the tree builder are iterative with an explicit stack bounded by the depth limit (R-GEN-6). A close tag that does not match, a duplicate attribute on one element, content after the root, or an unterminated construct is a reject. |
| R-DUI-9 | MUST: tag, attribute and function names are ASCII, start with a letter or `_`, and are folded with ordinal ASCII case folding only. Values are never folded. No locale-dependent comparison anywhere. |
| R-DUI-10 | MUST: an unknown tag used as a selector, and an unknown attribute, are kept as opaque names within the limits and counted; they do not fail the parse. The same holds for an unknown condition name in `<if>` or on a selector. An opaque condition is never guessed: a consumer that does not define the name evaluates it as false, so the rules under it do not apply. |

### 9.3 Value expressions

| Id | Requirement |
|---|---|
| R-DUI-11 | MUST: the expression parser is iterative or depth-counted, with nesting and argument limits from 9.5. A known function is checked for argument count and kind; a mismatch is a reject. An unknown function is kept as an opaque call, never evaluated, and counted. |
| R-DUI-12 | MUST: numbers are parsed with overflow checks (R-GEN-3) and range-checked: integers within plus or minus 1,000,000, colour components 0..255, `rp` lengths within the same integer range. Out of range is a reject. |
| R-DUI-13 | MUST: `library(name)` is data, not a path. The name is at most 32 characters of `[A-Za-z0-9_.-]`, with no separator, colon or leading dot. The core never loads anything. An integration resolves it only against a fixed allow-list of system modules, maps the module as data from the system directory by full path, and reads resources through a reader that meets section 3. A name outside the allow-list makes the value unresolved, never a load attempt. |
| R-DUI-14 | MUST: resource ids in `resstr`, `icon` and `graphic` are 1..65,535; `icon` and `graphic` sizes are 1..1,024. They are resolved only through the bounded reader. Images reach a decoder only after a bounded header check for their format (R-RES-3 for PNG; a DIB check with the same intent is written before any bitmap from a style is decoded). |
| R-DUI-15 | MUST: theme-query arguments (`gtc`, `dtb`, `gtf`, `gtmar`, `gtmet`) are a class name of at most 59 characters and integers in the ranges of R-V4-5. `sysmetric(n)` takes an index from a fixed allow-list. Every value that comes back from the theme-query interface or a system metric is clamped before use (R-MOD-3). |
| R-DUI-16 | MUST: nothing in a UIFILE is executed or expanded without a budget. If `macro` or `bind` expansion is ever implemented for consumer-owned layouts, it is non-recursive and counts every produced node against the node limit. |

### 9.4 Process

| Id | Requirement |
|---|---|
| R-DUI-17 | MUST: libFuzzer harnesses for the XML tokenizer and for the expression parser exist in the same commits as those modules, build with ASan + UBSan, and run in CI on generated seeds (R-PROC-1). |
| R-DUI-18 | MUST: D1 is not used on files not built by us before the DirectUI parsers have a row in `docs/security/BOUNDED-VALIDATION.md`, that row passed on the release candidate, and the security review of D1 is closed. |

### 9.5 Limits (per UIFILE resource)

| Quantity | Corpus | Limit |
|---|---|---|
| Resource size | 89,116 bytes maximum | 512 KB |
| Element nesting depth | 14 | 32 |
| Elements (nodes) | 6,280 `element` tags over 127 resources | 16,384 |
| Attributes per element | not measured | 64 |
| Attributes in total | `id` alone 7,516 over the corpus | 65,536 |
| Name length (tag, attribute, function, resid) | not measured | 64 characters |
| Attribute value length | not measured | 4,096 bytes |
| Style sheets | 415 over the corpus; 22 in one theme resource | 512 |
| Selectors + conditions | 2,152 + 9,084 over the corpus; 634 rules + 544 conditions in one theme resource | 16,384 |
| `<if>` scopes (theme sheets) | 537 in one theme resource | 4,096 |
| Kept declarations (theme sheets) | 1,146 in one theme resource | 16,384 |
| Value nodes in total (theme sheets) | 4,611 in one theme resource | 65,536 |
| Expression nesting | not measured (shapes show 2 to 3) | 8 |
| Arguments per call | 4 observed | 16 |
| Expression nodes per value | not measured | 256 |
| Comments | 52 over the corpus | counted in the size limit only |

## 10. Open points

- R-V4-3, R-V4-4: two format facts the sketch must state (VMAP prefix unit, walk end on unaligned sizes).
- R-V4-10: `BCMAP` semantics: VERIFIED in the sketch (count + one base ordinal per class, shifted by the descriptor names; acyclic, depth at most 6 in the corpus); the L1 validator enforces the rule.
- R-V4-2: that the lowest ordinal wins a lookup by name is our rule; which duplicate Windows uses is a HYPOTHESIS, not measured. If a later measurement shows the other one, the rule changes here by commit.
- R-MOD-6: that every base chain ends in `globals` is the rule of the current engine; what Windows does for a version-4 style is a HYPOTHESIS, not measured.
- R-MOD-7, section 6.1: the ranges are set from what is safe for the consumer, and only the pixel range is confirmed on the corpus (maxima below 4,000). Open: a run of the range functions over the 32 reference styles that reports every rejected property. The expected count is 0; a rejected corpus value is brought to the security reviewer and the table changes by commit, the code does not get an exception. Also open: whether Windows treats a BOOL other than 0 and 1 as true (the corpus has only 0 and 1).
- R-RES-3: the IHDR set and the chunk set are confirmed on the corpus (30,171 images of the 32 reference styles, `docs/design/L3-IMAGES-DESIGN.md` section 2). Open: whether a third-party style outside the corpus uses `tRNS` with colour type 2 or a palette; such a style is rejected until the rule is widened here. Whether the decoder renders differently without `gAMA` and `sRGB` is a HYPOTHESIS, not measured; the rule keeps them, so it does not matter today.
- R-PE-8: strict ordering must be confirmed on the corpus by the L0 corpus run; if a reference style is unordered, the rule becomes "linear scan, first match" by commit here.
- Record types 240, 241, 243 and the records with a non-zero reference on value types are dropped by R-V4-8 until the sketch explains them.
- R-DUI-6: whether any reference UIFILE holds bytes from 0x80 that are not UTF-8 (D0 says "UTF-8/ANSI"). If one does, the rule is revisited here.
- R-DUI-7: whether any reference style sheet needs CDATA or a numeric character reference.
- Section 9.5: the rows marked "not measured" are set with headroom from the shapes in D0; the D1 corpus runner reports the real maxima and the limits are then confirmed or tightened by commit.
- R-DUI-14: stock `shellstyle.dll` carries `RT_BITMAP` images. No bitmap from a style is decoded in D1; the DIB header check is specified before the milestone that renders them.
