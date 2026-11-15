# L2 design note: property model, query subset, v3 adapter

Draft for review (Ryan, 2026-11-15), requested by god while the L1 fix-up is reviewed. **Design only: no code until the
L1 merge and the S1 report (R-PROC-3).** Labels as everywhere in `docs/`: VERIFIED (measured on the 32 reference
styles or in the lab), SOURCE (`path:line`, LiberNT tree cand3 6f817ad0bf7 for the uxtheme engine, this repository for
the loader), HYPOTHESIS. `docs/security/REQUIREMENTS.md` is normative; where this note proposes a new rule it says so
and Jim 2 decides.

## 1. What L2 is and is not

The README names L2 as "property model with binary values, plus a query API mirroring the `GetTheme*` subset. A v3
adapter keeps the current engine's behaviour." This note splits that into:

- **L2 core** (`loader/`, C11, host-portable, no OS call, R-GEN-1): a model built from a validated style, binary
  getters, and the lookup that mirrors the engine's search order. Tests, corpus columns and a fuzz harness in the same
  commits. This is the deliverable of the L2 milestone.
- **v3 adapter** (uxtheme side): how `dll/win32/uxtheme` would consume the core without changing the version-3 path.
  Designed here, implemented only in the integration milestone I1 (owner-gated, README "Integration"). Nothing in
  LiberNT changes for L2.

Not in L2: string-table reading beyond what L1 has (`tl4_v4_find_string`), PNG chunk walking, decoding, drawing. Those
are L3 and the integration.

## 2. What the model may trust (from L1)

After `tl4_pe_open` and `tl4_v4_validate` return `TL4_OK` (SOURCE `loader/src/v4.c`, `tl4_v4_validate`):

| guarantee | where it comes from | what L2 does with it |
|---|---|---|
| every record header is in range; inline `cbData` is exact for its type (INT/BOOL/ENUM/COLOR/SIZE 4, POSITION 8, RECT/MARGINS 16, INTLIST `4 + 4·count`, `count ≤ 402`, STRING even ≤ 4,096 with a final NUL); FONT is header-only with `cbData` 92 | `tl4_v4_walk_records`, R-V4-5/6 | a getter copies exactly that many bytes and never re-parses (R-MOD-2) |
| a reference is an id (string, IMAGE, STREAM) and resolves through the reader; unresolved ones are counted, fonts fatal, over 1% fatal | `resolve_cb`, R-V4-7/12 | the model stores the id; resolution at query time is a binary search (R-V4-14) |
| `classId < CMAP count`; `partId`, `stateId ≤ 255`; `nameId` 1..65,535 | R-V4-5 | the model indexes by them without further checks |
| BCMAP: one base per class, acyclic, depth ≤ 8 | `tl4_v4_check_bcmap`, R-V4-10 | the override chain of section 4 is walked at most 8 + 1 steps |
| the counts in `tl4_v4_stats`: `cmap_names`, `variant.records`, `rmap.records`, `variant.partstates` and `variant.classes` (upper bounds), `images`, `streams`, dropped records | R-V4-12/13 | they size the model before anything is allocated (R-GEN-7) |

The validator does not guarantee: record order, uniqueness of (class, part, state, nameId) (R-V4-9), uniqueness of
class names (R-V4-2 as ruled), that the bytes are the same at query time (R-GEN-11). The model handles each of these
below.

## 3. The model (L2 core)

### 3.1 Shape

Four flat arrays in one caller-provided buffer, sized from the statistics, plus the two mappings the validator saw.
Pointers into the input are not stored; every value is an `(offset, size)` into the style's bytes, re-checked against
`pe->size` at copy time (proposed R-MOD-4, section 7).

```
tl4_model
  const tl4_pe *style, *strings;         /* the validated mappings; strings may be NULL (R-RES-1) */
  uint32_t nclasses;  tl4_class *classes;          /* one per CMAP ordinal, descriptors included (inert) */
  uint32_t npartstates; tl4_partstate *partstates; /* grouped by class, sorted by (part, state) */
  uint32_t nprops;    tl4_prop *props;             /* grouped by part-state, sorted by nameId */
  uint32_t nnames;    uint32_t *name_order;        /* CMAP ordinals sorted by name: lookup by name (section 4.3) */
  tl4_model_counts counts;                         /* true part-states, duplicates dropped, unknown types dropped */

tl4_class      { uint32_t name_off, name_chars;    /* into the CMAP resource: "App::Class" */
                 uint32_t base;                    /* BCMAP base ordinal or TL4_NO_BASE */
                 uint32_t first_ps, nps; }
tl4_partstate  { uint32_t class_ordinal; uint16_t part, state; uint32_t first_prop, nprops; }
tl4_prop       { uint16_t name_id; uint8_t type; uint8_t origin;   /* PO_* of section 4.2 */
                 uint32_t reference;                /* 0 or the id (R-V4-7) */
                 uint32_t data_off, cb;             /* inline bytes inside the VARIANT or RMAP resource, or 0 */
                 uint16_t area; }                   /* which record area (VARIANT or RMAP) data_off refers to */
```

Sizes at the limits of section 6: 1,024 classes × 24 B + 8,192 part-states × 16 B + 32,768 props × 20 B + 1,024 × 4 B
≈ 0.8 MB, fixed by `tl4_model_size(const tl4_v4_stats *)`; the caller allocates (R-GEN-1: the core calls no allocator)
and `tl4_model_build` refuses a smaller buffer. The RMAP records (14 per reference file, VERIFIED) go into the same
arrays under class 0 so `GetThemeDocumentationProperty` can use the same lookup.

### 3.2 Build (R-MOD-1: O(n log n), no search per record)

1. **Names once.** One scan of the CMAP fills `classes[i].name_off/name_chars` (the same scan `tl4_v4_cmap_name`
   does, done once for all ordinals, not per record: R-MOD-1 and the `v4.h` note). Base ordinals come from
   `tl4_v4_bcmap_base`. The leading descriptors (`documentation`, `sizevariant.*`, `colorvariant.*`) get `base =
   TL4_NO_BASE` and no part-states; they stay addressable so every ordinal a record names is valid.
2. **Records in file order.** A second `tl4_v4_walk_records` over RMAP then VARIANT appends one `tl4_prop` per
   accepted record, with a running file index. The callback stops with an error when the count reaches the number
   the first pass reported: this is the second-pass check R-GEN-11 asks for (the bytes may have changed; the model
   never writes past what the first pass sized). Unknown types and reference-on-value records are already dropped and
   counted by the walk (R-V4-8).
3. **Sort.** Heapsort of the props by `(class, part, state, nameId, file index)`: in place, iterative, no allocation,
   no recursion (R-GEN-6/7), worst case 32,768 × 15 comparisons. Then one pass folds runs of equal
   `(class, part, state)` into `partstates` (the **true** part-state count: this replaces the validator's upper bound,
   as the `v4.h` note promises) and runs of equal class into `classes[].first_ps/nps`.
4. **Duplicates** `(class, part, state, nameId)`: the first in file order wins, later ones are dropped and counted
   (R-V4-9). The type of the surviving record is stored; a query that asks for another primitive type gets
   "unsupported", as the engine does today (`MSSTYLES_PSFindProperty` matches primitive and id).
5. **Name order.** Heapsort of the 1,024 ordinals by name (code unit order, then length; the same comparison the PE
   reader uses for R-PE-8), stable on ordinal, so equal names keep ascending ordinals and the lowest wins a lookup
   (R-V4-2 as ruled). Duplicate names are counted in `counts`.
6. **Origin** per prop (section 4.2) is computed here, once, from where the record lives: state ≠ 0 → `PO_STATE`;
   part ≠ 0 → `PO_PART`; the `globals` class → `PO_GLOBAL`; else `PO_CLASS`. This is the rule of
   `MSSTYLES_AddProperty` (SOURCE msstyles.c:785-791).

Cost at the limits: two record walks (bounded by L1's test: 0.009 s for open + validate in a release build), one
heapsort of 32,768 elements, one of 1,024. The R-V4-14 test gains a model build and the same CPU bound.

### 3.3 Binary getters (R-MOD-2)

Every getter takes a `const tl4_prop *` the lookup returned, checks `type`, re-checks `data_off + cb ≤ resource size`
and `resource ≤ pe->size` (R-GEN-11), then copies little-endian words out:

| getter | type | result | notes |
|---|---|---|---|
| `tl4_get_int`, `tl4_get_bool`, `tl4_get_enum`, `tl4_get_color`, `tl4_get_size` | 202/203/200/204/207 | `int32_t` / `uint32_t` COLORREF | BOOL is 0/1 after the copy (any non-zero → 1, HYPOTHESIS that Windows does the same; the corpus has only 0 and 1, VERIFIED) |
| `tl4_get_position` | 208 | two `int32_t` | |
| `tl4_get_rect`, `tl4_get_margins` | 209/205 | four `int32_t` | field order left, top, right, bottom for RECT and left, right, top, bottom for MARGINS, the Windows struct order (HYPOTHESIS until a part is drawn and compared, sketch 1.3) |
| `tl4_get_intlist` | 211 | count + up to 402 `int32_t` into the caller's buffer | count was validated (R-V4-6) |
| `tl4_get_string` | 201 | UTF-16 copy with the caller's capacity, or the referenced string-table string through `tl4_v4_find_string` when `reference != 0` (R-V4-7 as amended) | truncation is a "buffer too small" error, never a silent cut (R-GEN-5) |
| `tl4_get_font` | 210 | `tl4_font { uint16_t face[32]; int32_t points; uint8_t bold, italic, underline, strikeout; uint8_t quality; }` | the string `reference` is read through `tl4_v4_find_string` (style first, then `strings`), at most 255 characters, face at most 31 (R-RES-2), and parsed by a bounded tokenizer of the syntax every corpus font string uses: `face, size[, Bold][, Italic][, Underline][, Quality:X]` (VERIFIED on all 12 ids of the stock styles and the inline fonts of the third-party ones). Any other syntax is an error, not a default font (R-GEN-5) |
| `tl4_get_filename` | 206 | the IMAGE id and the `(offset, size)` of the IMAGE resource via `tl4_pe_find` (binary search, R-V4-14) | the bytes are handed to the L3 chunk walker, never to a decoder directly (R-RES-3) |
| `tl4_get_diskstream` | 213 | the STREAM id and its `(offset, size)` | same |

Clamps (R-MOD-3) are **not** applied inside the getters: the core returns what the style says, and a single
documented table (section 6) is applied by the adapter at the boundary to the system (metrics) and before any divisor
or allocation. Keeping the clamps out of the getters keeps the core's answer equal to the file (R-GEN-8) and testable
against the corpus dump.

## 4. The query subset

### 4.1 Which exports

The engine's public getters all go through `MSSTYLES_FindProperty` then one `MSSTYLES_GetProperty*` (SOURCE
property.c:40-300: `GetThemeBool/Color/EnumValue/Filename/Font/Int/IntList/Position/Rect/String/Margins/Metric/
PropertyOrigin`), the metrics through `MSSTYLES_FindMetric` (metric.c `GetThemeSysBool/Color/Font/Int/Size/String`),
`IsThemePartDefined` through `MSSTYLES_FindPart` (system.c:1111-1120), and the draw code through
`MSSTYLES_FindProperty(TMT_FILENAME, …)` + `MSSTYLES_LoadBitmap` (draw.c:211-327). The L2 core mirrors exactly that
set:

| engine export | core query | notes |
|---|---|---|
| `GetThemeBool/Color/Int/IntList/Position/Rect/Margins/String` | `tl4_model_find` + the getter of 3.3 | one lookup, one copy |
| `GetThemeEnumValue` | `tl4_model_find` + `tl4_get_enum` | v4 stores the integer; no name table needed (the engine's `MSSTYLES_LookupEnum` serves only the INI path) |
| `GetThemeFilename` | `tl4_get_filename` | the engine returns a file name string; for v4 the adapter returns a synthetic `#<id>` (HYPOTHESIS: what Windows returns for a v4 style is not measured; no shipped caller is known to depend on it) |
| `GetThemeFont` | `tl4_get_font` | the adapter converts points to `lfHeight` with the DC's `LOGPIXELSY` exactly as msstyles.c:1804 does today |
| `GetThemeMetric` | `tl4_model_find` with any type, then the first word of the value | the engine's rule (property.c:217-226: POSITION → x, MARGINS → cxLeftWidth, INTLIST → first) |
| `GetThemePropertyOrigin` | `tl4_prop.origin` | computed at build (3.2 step 6) |
| `GetThemeSys*` | `tl4_model_find` on the `sysmetrics` class, part 0, state 0 | every reference style has a `sysmetrics` class with 51 properties and 6 font references (VERIFIED, class-usage table of 01); it replaces the engine's separate `tf->metrics` list (`[SysMetrics]`) |
| `IsThemePartDefined` | `tl4_model_part_defined(class, part)`: a part-state `(part, 0)` exists in the class or its chain | the engine's `MSSTYLES_FindPart` (msstyles.c:532-545) walks the overrides too |
| `GetThemeDocumentationProperty` | `tl4_model_find` on class 0 (RMAP records 600-608) + `tl4_get_string` | the engine reads the INI `[documentation]` section (system.c:1129); cheap to support, listed as optional |
| draw.c image path | `tl4_get_filename` / `tl4_get_diskstream` | bytes to L3 |

Not mirrored in L2: `GetThemeTransitionDuration` (INTLIST 6000, trivial later), `EnumThemeColors/Sizes`,
`GetThemeDefaults` (VMAP strings: adapter, section 5), `GetThemeBitmap/GetThemeStream` (stubs in the engine today,
draw.c:2132-2143), everything that draws.

### 4.2 Search order (mirrors the engine)

`tl4_model_find(model, class, part, state, name_id, &prop)` reproduces `MSSTYLES_FindProperty` (SOURCE
msstyles.c:1421-1445) and `MSSTYLES_FindPartState` (msstyles.c:564-575):

1. `(part, state)` in the class, then in each class of its override chain;
2. if `state ≠ 0`: `(part, 0)` along the whole chain;
3. if `part ≠ 0`: `(0, 0)` along the whole chain;
4. not found.

Each step is a binary search of the class's part-states by `(part, state)` and of the part-state's props by `nameId`:
at most (8 + 1) × 3 × (13 + 15) comparisons per query. The **override chain** of a class is its BCMAP base, then the
base's base, …, at most 8 steps (R-V4-10), and then `globals` once if the chain did not end there. That last step is
the engine's rule (msstyles.c:1156-1166 appends `globals` to every chain) and a HYPOTHESIS for Windows; it is what "the
adapter keeps the current engine's behaviour" means, recorded as an open point (section 8).

### 4.3 Class lookup by name

`tl4_model_class(model, utf16 name, chars)` → the lowest ordinal whose CMAP name equals the query exactly (R-V4-2 as
ruled; code-unit comparison, no folding, no locale: R-GEN-8). The engine's class-list semantics stay in the adapter:
`OpenThemeData`'s `"A;B;C"` list, the `App::Class` try before `Class` (`MSSTYLES_OpenThemeClass`, msstyles.c), and
`SetWindowTheme`'s sub-app name. The adapter composes `App::Class` (both halves ≤ 59 characters, R-V4-2) and asks the
core once per candidate: a binary search over `name_order`.

## 5. The v3 adapter (design for I1, not L2 code)

Principle from the owner order: the version-3 engine stays as it is; version 4 is a second producer of the same
in-memory lists, and the getters learn one `if`.

1. **Open.** `MSSTYLES_OpenThemeFile` (msstyles.c:99) reads `PACKTHEM_VERSION`; version 3 continues unchanged;
   version 4 maps the file read-only from its own handle (R-POL-2: `CreateFile` + `GetFileSizeEx` +
   `CreateFileMapping(PAGE_READONLY)` + `MapViewOfFile`, size from that handle), runs `tl4_pe_open` and
   `tl4_v4_validate` on the mapping, allocates `tl4_model_size` bytes and builds the model. Any failure closes the file
   and returns `ERROR_BAD_FORMAT`: fail closed, unthemed (R-POL-4). The crash mark (R-POL-5) is set before the build
   and cleared after. `LoadLibraryEx(LOAD_LIBRARY_AS_DATAFILE)`, `FindResource` and `LoadString` are not used for a
   v4 style at all: the bounded reader does every lookup.
2. **Colour and size names.** `pszAvailColors`/`pszAvailSizes` and the selected names come from VMAP (`NormalColor`,
   `NormalSize`, the only values in any file, VERIFIED) so `EnumThemeColors`, `EnumThemeSizes`, `GetThemeDefaults` and
   the `ThemeManager` registry values keep working without change.
3. **Lists.** Where `MSSTYLES_ParseThemeIni` would run, a `MSSTYLES_BuildFromModel` creates the `THEME_CLASS` /
   `THEME_PARTSTATE` / `THEME_PROPERTY` lists from the model: one `THEME_CLASS` per class with part-states (the
   descriptors get none), `szAppName`/`szClassName` split at the `::` as `MSSTYLES_ParseIniSectionName` does
   (msstyles.c:389), `overrides` set from the chain of 4.2, `isGlobal` for `globals`, the `sysmetrics` class copied into
   `tf->metrics` so `MSSTYLES_FindMetric` and the metric application in `MSSTYLES_SetActiveTheme` (msstyles.c:918-981)
   stay untouched. Every `MSSTYLES_Find*` then works as today, by construction. A `THEME_PROPERTY` of a v4 style has
   `lpValue = NULL`, `dwValueLen = 0` and a new `const tl4_prop *v4` (sketch option (b)).
   Alternative, later: make `MSSTYLES_FindProperty` call `tl4_model_find` directly and drop the node copies. Not for
   the first integration: the list form keeps the engine's search order by construction and is what the existing
   tests exercise.
4. **Getters.** Each of the nine `MSSTYLES_GetProperty*` (msstyles.c:1723-1915) starts with
   `if (tp->v4) return MSSTYLES_V4_GetProperty*(tp, …)`, a thin call into the core's getter of 3.3 plus the clamp of
   section 6. `GetPropertyFont` converts `tl4_font` to `LOGFONTW` with the existing height formula (msstyles.c:1804).
   `GetThemeEnumValue` (property.c:74) for a v4 property returns the integer without `MSSTYLES_LookupEnum`.
5. **Images.** `MSSTYLES_LoadBitmap` (msstyles.c:1648) keys its `THEME_IMAGE` cache by file name. A v4 FILENAME is an
   id: a parallel `MSSTYLES_LoadBitmapById(tc, id)` with an `id` field in `THEME_IMAGE`, fed by `tl4_get_filename` →
   L3 chunk walker (R-RES-3/4) → `MSSTYLES_TryLoadPng` (pngsup.cpp). The four call sites in draw.c (211-327) go
   through one helper that dispatches on `tp->v4`. The `IMAGE` name convention of v3 (`\ / .` → `_`) is untouched.
6. **Metrics to the system.** `MSSTYLES_SetActiveTheme(setMetrics)` applies colours, fonts and sizes through
   `SystemParametersInfo`; for v4 the same code reads `tf->metrics` built in step 3, with the clamp table of section 6
   applied before any value leaves (R-MOD-3). The clamps should be applied to the v3 path too (the sketch's "same
   clamps as the v3 path should get"); that is a separate, small change the owner approves with I1.
7. **Install.** The same `tl4_pe_open` + `tl4_v4_validate` + `tl4_model_build` run in the low-privilege install step
   (R-POL-3, R-GEN-10) and a style that fails is never offered.

## 6. Clamp table (R-MOD-3, applied by the adapter)

| value | range | why |
|---|---|---|
| any INT, SIZE, POSITION, RECT, MARGINS component that becomes a pixel length or offset | −8,192 .. 8,192 | the largest image side allowed is 8,192 (section 6 limits); corpus maxima are below 4,000 (VERIFIED, sketch 3) |
| `TMT_IMAGECOUNT`, `TMT_GLYPHINDEX` and every count used as a divisor or an index into an image strip | 1 .. 256 | the engine divides the image size by `imagecount` (draw.c) and the strip index must stay inside the bitmap; 0 and negatives are rejected as "unsupported" |
| font size (points) | 1 .. 255 | R-RES-2's string bound; the corpus uses 8..12 (VERIFIED) |
| font face | ≤ 31 characters, NUL-terminated | `LF_FACESIZE` 32, R-RES-2 |
| system metric sizes (`TMT_SIZINGBORDERWIDTH`, `TMT_SCROLLBARWIDTH`, caption and menu heights, …) | 0 .. 512 | `SystemParametersInfo` would otherwise accept absurd values; the corpus uses 1..40 (VERIFIED on `sysmetrics`) |
| INTLIST count | 0 .. 402 | `MAX_INTLIST_COUNT` (uxtheme.h:146), validated already |
| `TMT_TRANSITIONDURATIONS` entries, `TMT_MINDPI1..7` | 0 .. 65,535 | avoid overflow in later arithmetic |

A clamped value is a reject of that property ("unsupported"), not a repaired value (R-GEN-5), except where the engine's
own default already applies (a missing property). Which of the two happens is decided per property in I1, with Jim 2.

## 7. Proposed requirements for Jim 2 (not normative until he writes them)

- **R-MOD-4**: the model stores offsets and sizes into the mapping, never pointers; every getter re-checks the range
  against the mapping size before it copies (R-GEN-11 at query time).
- **R-MOD-5**: the core allocates nothing itself; `tl4_model_size(stats)` gives the exact size, the caller provides the
  buffer, and the build fails if a second walk yields more than the first pass counted (R-GEN-7, R-GEN-11).
- **R-MOD-6**: the search order of 4.2 and the `globals` tail are the engine's behaviour, recorded as such; a change
  needs a measurement on Windows and a commit here.
- **R-MOD-7**: the clamp table of section 6 is applied at the boundary (adapter), not in the core; the core returns the
  file's values so the corpus dump stays the test oracle.

## 8. Tests, corpus, fuzzing for L2

- Unit (`loader/tests/test_model.c`, generated fixtures): build from `v4f_default`; every getter against the fixture's
  known values; search order (state → part → class → base → `globals`) with a fixture whose chain is 3 deep; duplicate
  `(class, part, state, nameId)` records: first wins, counted; duplicate class names: lowest ordinal, counted; a
  property asked with the wrong primitive is "unsupported"; too-small buffer refused; font string syntax negatives;
  R-GEN-11: a model built, then the buffer overwritten, every getter rejects or stays in range.
- Limits: `test_v4_limits.c` gains the model build and the same 5 s CPU bound (release) and completion-only under the
  sanitizers; memory equals `tl4_model_size` exactly.
- Corpus (`corpus_v4.c`): new columns true part-states, duplicate records dropped, duplicate class names, model size;
  the 32 rows are filled from the run (the three Aero10 Metro styles report 1 duplicate name each, VERIFIED on CMAP).
  A spot check of known values from the format analysis (stock aero `globals` colours 3804/3805, `MINCOLORDEPTH` 15,
  `Button` part 1 margins) through the getters turns the inline encodings' VERIFIED status into a test.
- Fuzz (S2, harness in the first L2 commit as REQUIREMENTS demands): `fuzz_v4_model`: open, validate, build into an
  exact-size buffer, then queries driven by the input's tail bytes (class by name and ordinal, part, state, name id,
  every getter): promises are determinism (field compare, never memcmp), every returned range inside the mapping, no
  read outside the sizes the validator checked (ASan), and the model's counts within the stats' bounds. The
  overwrite-after-build step of S1 section 4 applies to the getters.

## 9. Open points

1. `globals` as the tail of every BCMAP chain: engine behaviour (SOURCE msstyles.c:1156-1166), Windows HYPOTHESIS.
2. `GetThemeFilename` on a v4 style: synthetic `#<id>` (HYPOTHESIS); measure on the host when the owner allows.
3. RECT/MARGINS field order and BOOL normalisation (HYPOTHESIS until the draw-by-hand check of the sketch, 4.2).
4. Enum values: v4 stores integers the engine switches on with defaults; whether to range-check them against
   `vssym32.h` enum tables (none needed for safety, perhaps for fidelity).
5. Types 240/241/243 and ids 5100-5146 stay dropped (R-V4-8): high-contrast colours in aerolite are lost (HYPOTHESIS).
6. The reference field on the 10 RECT + 1 BOOL records of stock aero (sketch 5.2): dropped as reference-on-value today;
   if they matter for drawing, L3 or later.

## 10. Evidence

`docs/design/V4-LOADER-SKETCH.md` sections 1-3; `loader/src/v4.c`, `loader/src/pe.c` at loader/l1 1487400;
LiberNT cand3 6f817ad0bf7 `dll/win32/uxtheme/{msstyles.c,msstyles.h,property.c,metric.c,draw.c,system.c}`,
`sdk/include/psdk/uxtheme.h`; class-usage table of the format analysis (research-dwm 01, `ryan/class-usage.txt`:
`sysmetrics` (51 properties, 1 part-state, 6 references) and `globals` present in all 32 styles).
