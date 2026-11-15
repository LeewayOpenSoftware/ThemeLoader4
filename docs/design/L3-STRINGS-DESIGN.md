# L3 design note: the bounded string-table reader

Ryan, 2026-11-15, for Jim 2's ruling before the code (owner decision 1 of 2026-11-15: the string reader belongs to the
remaining validation before production-ready). Labels as everywhere in `docs/`: VERIFIED (measured on the 32
reference styles and the two stock `.mui`, read-only), SOURCE (`path:line`; LiberNT cand3 6f817ad0bf7 for the engine),
HYPOTHESIS. `docs/security/REQUIREMENTS.md` is normative; section 6 proposes amendments and Jim 2 decides.

## 1. What exists and what is missing

L1 added `tl4_v4_find_string(pe, id, &utf16, &chars)` (SOURCE loader/src/v4.c): block `id / 16 + 1` of resource type 6
through the bounded reader, 16 length-prefixed strings, each length checked against the block, the wanted one returned
as a pointer into the mapping when 1..255 characters long. The validator resolves every FONT and STRING reference with
it, the style first, then the explicitly opened strings module (R-RES-1), and L2 stores the resolved range in the
model. That is enough to validate and to serve fonts and record strings.

Missing for the README's L3 line and for the adapter:

1. **Whole-table validation.** Only referenced strings are read today. The engine reads strings the records do not
   reference: colour and size display names and tooltips (`EnumThemeColors`/`EnumThemeSizes`: `LoadStringW(hTheme,
   resourceId)` and `resourceId + 1000`, SOURCE system.c:1477-1480) and the documentation properties of a version-3
   style (`GetThemeDocumentationProperty`: `LoadStringW(hTheme, 5000 + n)`, SOURCE system.c:1134-1163). A version-4
   style carries its documentation as RMAP STRING records 601-608 referencing string ids 101-112 (VERIFIED, all 32),
   so the adapter will read those through L2; but every block an adapter may touch must have passed the same shape
   check as the referenced ones, at install and at load (R-GEN-10).
2. **A copying getter.** `tl4_v4_find_string` hands out a pointer; R-MOD-4 wants one copy, range-checked, with every
   further check on the copy, and a caller buffer that is never truncated silently (R-GEN-5).
3. **The language policy.** R-RES-1 forbids MUI redirection; which `.mui` the consumer opens, and what happens when
   the style has both inline strings and a `.mui`, is undecided.
4. **A fuzz harness for the table** (the S1 plan's `fuzz_strings` does not exist; L1 reaches the reader only through
   `fuzz_v4_style` and the blob harness's selector 4).

## 2. What the corpus says (VERIFIED, 2026-11-15, read-only)

| fact | value |
|---|---|
| where the strings live | the 2 stock styles: no inline block, 5 blocks each in `en-US\<name>.msstyles.mui` (language 1033); the 30 third-party styles: 4..61 inline blocks, language 0 (neutral), no `.mui` |
| totals | 844 blocks, 13,504 string slots, 1,091 non-empty strings; 0 blocks with a bad shape (every block is exactly 16 length-prefixed strings ending on its size) |
| lengths | longest string 72 characters (a copyright notice at id 106 of Aero10 Metro); the 675 font strings are 24..40 characters; no control character in any string |
| largest block | 668 bytes; the most blocks in one module 61 (the three Aero 7 builds: 34 non-empty strings spread over 61 blocks, mostly empty slots) |
| references | FONT 4,209 and STRING 384 records across the 32 styles, every one resolved (0 unresolved); the STRING ones are the RMAP documentation records 601-608 → string ids 101-112 |
| languages | one language per block in the 32 version-4 styles and the two `.mui`; the stock `.mui` block ids are the same block ids a self-contained style would carry. The shipped version-3 style is different: see section 9 |

Windows' `RT_STRING` format (SOURCE: the resource compiler's layout, confirmed by every block above): block `b`
holds the 16 strings with ids `16·(b-1) .. 16·b - 1`; each is a 16-bit character count followed by that many UTF-16
code units, no terminator; an absent string is a count of 0.

## 3. The reader (L3b)

### 3.1 API (loader/include/tl4/loader/strings.h; the L1 function stays and becomes a wrapper; language parameter added in section 9)

```
#define TL4_STR_MAX_CHARS    1024u     /* one string (corpus 72; fonts bounded further by R-RES-2 to 255) */
#define TL4_STR_MAX_BLOCKS   4096u     /* string ids 0..65,535 fill at most 4,096 blocks */
#define TL4_STR_MAX_BLOCK    (16u * (2u + 2u * TL4_STR_MAX_CHARS))

typedef struct tl4_strings_stats { uint32_t blocks, strings, max_chars, longest_id; } tl4_strings_stats;

/* Every type-6 block of the module: 16 length-prefixed strings, each inside the block, each <= TL4_STR_MAX_CHARS,
 * ending exactly on the block's size; block names 1..TL4_STR_MAX_BLOCKS, one language (R-PE-8). Reject, not skip. */
tl4_status tl4_strings_check(const tl4_pe *pe, tl4_strings_stats *stats);

/* Locate string `id` (0..65,535): (offset, chars) inside the mapping, re-checked (R-GEN-11); TL4_E_NOT_FOUND for an
 * absent block or an empty slot. The L1 tl4_v4_find_string calls this. */
tl4_status tl4_strings_locate(const tl4_pe *pe, uint32_t id, uint32_t *offset, uint32_t *chars);

/* Copy string `id` into out[cap_chars], NUL-terminated, *chars without the NUL: one copy, then the checks on the copy
 * (R-MOD-4); TL4_E_BUFFER when it does not fit (never truncated, R-GEN-5). */
tl4_status tl4_strings_get(const tl4_pe *pe, uint32_t id, uint16_t *out, uint32_t cap_chars, uint32_t *chars);

/* The pair a consumer holds: the style and, when the style carries no inline table, its explicitly opened .mui. */
typedef struct tl4_strings { tl4_pe style, mui; int has_mui; } tl4_strings;
tl4_status tl4_strings_open(const tl4_pe *style, const tl4_pe *mui_or_null, tl4_strings *out);   /* checks both tables */
tl4_status tl4_strings_get2(const tl4_strings *s, uint32_t id, uint16_t *out, uint32_t cap_chars, uint32_t *chars);
```

### 3.2 Rules

- **Whole-table check at validation.** `tl4_v4_validate` calls `tl4_strings_check` on the style and, when given, on the
  strings module, before it resolves any reference; a module with one malformed block is a reject (R-GEN-5, R-GEN-10:
  the same check at install and at load). Cost: O(total table bytes), 668 bytes × 61 blocks at most in the corpus.
- **Lookup is bounded and direct**: block id from the string id, one binary search through the reader (R-V4-14), one
  pass over at most 16 length prefixes.
- **The language policy (R-RES-1)**: the core never chooses a file. The consumer opens at most one strings module per
  style, the file at `<part>\<name>\<language>\<name>.msstyles.mui` of the fixed store layout (policy T-STORE-3), for a
  language it decides once (the adapter: the user's UI language, falling back to `en-US`; the corpus runner: `en-US`);
  the language directory name must satisfy T-STORE-4 (`[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,2}`) *before* it is put into a
  path, and a name that does not selects no module (review MUST 6, 2026-11-15; this is consumer work, I1). It is opened
  through the same reader and the same policy as the
  style itself (R-POL-1/2: a protected location, mapped read-only from its own handle). A style that carries an inline
  table is served from it first; the `.mui` answers only ids the inline table does not have (the L2 order). Both must
  pass `tl4_strings_check`. No `LoadLibraryEx`, no `LoadString`, no MUI fallback list (SOURCE for what is avoided:
  system.c:1160, :1477; R-PE-1).
- **Fonts** keep R-RES-2 (255 characters, face 31) on top of the table rule; the table rule is 1,024 for every other
  string (display names, tooltips, documentation, the copyright notice of 72 characters in the corpus).
- **Nothing is interpreted** in the core beyond length and UTF-16 code units; the consumer decides what a string
  means (R-MOD-7 spirit).

### 3.3 Limits (proposed additions to section 6)

| quantity | corpus maximum | proposed limit |
|---|---|---|
| string length | 72 characters (fonts 40) | 1,024 (`TL4_STR_MAX_CHARS`); fonts 255 (R-RES-2, unchanged) |
| blocks per module | 61 | 4,096 (`TL4_STR_MAX_BLOCKS`: the id space) |
| block size | 668 bytes | 16 × (2 + 2 × 1,024) = 32,800 bytes |
| strings resolved per style | 4,593 (FONT + STRING records) | bounded by the record limit (32,768), one lookup each (R-V4-14) |

## 4. Tests, corpus, fuzzing

- Unit (`test_strings.c`, generated fixtures through `v4b_string_block` and `pebuild`): every slot of a block
  addressable; empty slots `TL4_E_NOT_FOUND`; a block that ends early, late, or with a length past its end rejected;
  a string of 1,024 accepted and 1,025 rejected; block id 4,097 rejected; two languages for one block rejected (already
  R-PE-8); the copy getter's buffer rule; the style-then-mui order; the L1 function unchanged in behaviour (its tests
  stay green); a changed-bytes case (the string's length prefix altered after the check: the getter rejects).
- Limits: a module with 4,096 blocks of 16 strings of 1,024 characters validates inside the fuzz timeout (R-V4-14).
- Corpus: `corpus_v4.c` gains `string_blocks_ok` (must equal the module's type-6 count, stock: of the `.mui`),
  `strings_total`, `max_string_chars`; the maxima line reports them.
- Fuzz (S2): `fuzz_strings`: a PE image through `tl4_strings_check`, then `tl4_strings_locate`/`get` for ids taken from
  the input's tail bytes; promises: determinism, cleared stats on reject, every located range inside the mapping,
  `get` agrees with `locate`, a buffer one short fails with `TL4_E_BUFFER` and a full one succeeds. Seeds generated.

## 5. Adapter notes (I1, not L3 code)

- `EnumThemeColors`/`EnumThemeSizes`: for a version-4 style the names come from VMAP (one variant each, VERIFIED);
  the display name and tooltip the engine reads with `LoadStringW(resourceId)` / `+1000` have no version-4 source in
  the corpus (HYPOTHESIS: Windows uses the same inline ids; not measured, a vsprobe question) — the adapter returns the
  VMAP name as display name until measured.
- `GetThemeDocumentationProperty`: RMAP records 601-608 (TMT_DISPLAYNAME..TMT_DESCRIPTION, SOURCE vssym32.h:154-161)
  through `tl4_model_find_rmap` + `tl4_get_string` (L2), not through `5000 + n`.
- The engine's `LoadStringW(pt->hTheme, …)` calls (system.c:1160, :1477, :1480) are the v3 path and stay.

## 6. Proposed requirement changes for Jim 2

- **R-RES-1 (amend)**: "A `.mui` is opened explicitly" becomes: the consumer opens at most one strings module per
  style, chosen by it and located in the protected store next to the style, through the same reader and policy as the
  style; the core never selects or opens a file; inline strings take precedence over the module.
- **R-RES-8 (new)**: every string-table block of the style and of the strings module passes the shape check at
  validation (16 length-prefixed strings inside the block, ending on its size, each at most 1,024 characters); a
  module with one malformed block is rejected; a string is read by one bounded lookup and copied once.
- **Section 6**: the three limits of 3.3.

## 7. Open points

1. Windows' source for colour/size display names and tooltips in a version-4 style (HYPOTHESIS; vsprobe can measure
   `EnumThemeColors` on the host once the controlled environment exists).
2. Which `.mui` language the adapter picks when the user's language is absent (en-US fallback is the proposal).
3. VERIFIED 2026-11-15 (`tl4_loader_test_shipped`, gcc and ASan builds on libernt-srv, 1,254 checks): the 70 entries of
   the shipped version-3 style pass the exact shape check with nothing loosened: 5 blocks, 70 entries, 224 strings
   (1 to 8 per entry), longest 43 code units (id 5004), 14 languages per block; every entry serves at least one string
   in its own language and the lowest-id lookup equals the explicit one. The 32 corpus styles and the two `.mui`
   pass as well (844 entries, 1,091 strings, longest 72, one language everywhere).

## 8. Evidence

Measurement over the read-only work copies (research-dwm, 2026-11-15): blocks, slots, lengths, languages, references,
as quoted in section 2; `loader/src/v4.c` (`tl4_v4_find_string`), `loader/src/model.c` (resolution at build);
LiberNT `dll/win32/uxtheme/system.c` (LoadStringW uses), `sdk/include/psdk/vssym32.h`. No string of the corpus is
reproduced here beyond the length of one.

## 9. Amendment of 2026-11-15 (after R-PE-8 as amended, TL4 main b220401): languages

Sections 2 and 3 were written when R-PE-8 allowed one language entry per (type, name). The amended R-PE-8 allows 1..64
entries per pair, strictly ascending ids, and `tl4_pe_find` returns the lowest id with the count in
`tl4_resource.nlanguages`; the version-4 validator rejects any file with a multi-language pair (`nmulti_pairs != 0`),
and `PACKTHEM_VERSION` must have exactly one entry.

**VERIFIED** (2026-11-15, `tl4_loader_test_shipped` on the shipped `aero.msstyles` of the LiberNT CI image, our own
build output from `media/themes`, read on libernt-srv): 341 data entries in 276 pairs; 5 `RT_STRING` blocks, each with
14 language entries (the only multi-language pairs of the file); `PACKTHEM_VERSION` has one entry. So a version-4
style or `.mui` has single-language tables by construction, while the version-3 reader path (the shipped style, the
compatibility check the owner asked for) must read a table that has several languages per block.

### 9.1 Rules

- **Every entry is checked.** `tl4_strings_check` walks every language entry of every type-6 name (the enumeration
  already visits each entry) and applies the block shape check to each one; one malformed entry in any language rejects
  the module (R-GEN-5). `tl4_strings_stats` gains `entries` (data entries checked; `blocks` stays the pair count) and
  `max_languages` (entries of the widest block, 1 for every version-4 file, 14 for the shipped style).
- **Lookup language.** `locate`, `get` and `get2` take a `language` argument. `TL4_STR_LANG_LOWEST` (0xFFFFFFFF, outside
  the 16-bit id space; 0 is the neutral language the third-party styles use and stays a valid explicit value) selects
  the entry with the lowest id, exactly what `tl4_pe_find` returns under R-PE-8, so the version-4 path and the L1
  wrapper keep their behaviour. An explicit id 0..65,535 selects that entry or fails with `TL4_E_NOT_FOUND`; the core
  does no fallback between languages (the engine's `LoadStringW` MUI fallback chain is the version-3 path and stays
  there; R-RES-1: the consumer decides the language, once).
- **One new reader entry point**, `tl4_pe_find_language(pe, type, name, language, out)`: the leaf header check of
  `tl4_pe_find`, then a scan of at most `TL4_PE_MAX_LANGUAGES` (64) ascending entries that stops at the first id above
  the wanted one; bounded (R-V4-14), re-checked (R-GEN-11).
- **No code unit 0 inside a string** (review MUST 1): the check rejects the block, and the getter checks its copy
  again, because the getter ends the copy with a 0 and an embedded one would give two lengths for one string. The
  corpus has none.
- **A lookup does not rely on the validation** (review MUST 2, R-GEN-11): it takes the block range from the resource
  reader again, bounds the block (at most 32,800 bytes) and every count it passes against that range, and uses each
  count from one read. A count that does not fit, before or at the wanted string, is `TL4_E_STR_BLOCK`, never a
  wrong string. On every failure the outputs are cleared: offset, chars, the statistics, and the first unit of the
  buffer; a capacity of 0 is `TL4_E_BUFFER`, the capacity needed is chars + 1 (review MUST 4). A language value that
  is neither `TL4_STR_LANG_LOWEST` nor 0..65,535 is `TL4_E_ARG`, not "not found" (review MUST 5). In `get2` only
  `TL4_E_NOT_FOUND` from the style goes on to the module; any other status is returned (review MUST 3).

### 9.2 Signatures (replacing the three of 3.1)

```
#define TL4_STR_LANG_LOWEST 0xFFFFFFFFu
typedef struct tl4_strings_stats { uint32_t blocks, entries, strings, max_chars, longest_id, max_languages; } tl4_strings_stats;
tl4_status tl4_strings_locate(const tl4_pe *pe, uint32_t id, uint32_t language, uint32_t *offset, uint32_t *chars);
tl4_status tl4_strings_get(const tl4_pe *pe, uint32_t id, uint32_t language, uint16_t *out, uint32_t cap_chars, uint32_t *chars);
tl4_status tl4_strings_get2(const tl4_strings *s, uint32_t id, uint32_t language, uint16_t *out, uint32_t cap_chars, uint32_t *chars);
tl4_status tl4_pe_find_language(const tl4_pe *pe, const tl4_res_name *type, const tl4_res_name *name, uint32_t language, tl4_resource *out);
```

### 9.3 Tests and fuzzing added

- Generated: the changed-bytes case covers the wanted string and a count *before* it (review MUST 2); a code unit 0
  written after the check is refused by the getter (MUST 1); the review's limit test is the largest table that fits a
  file the reader accepts (32 MiB: about 1,020 full blocks), not 4,096 full blocks (134 MB).
- Generated: a block with two languages (1031 then 1033) where the same id has different text per language:
  `LOWEST` returns the 1031 text, explicit 1033 the other, explicit 1034 `TL4_E_NOT_FOUND`; the same block with the
  second language's table malformed (length past the end) rejects the whole module while the first language alone
  passes; 64 languages on one block pass the check, and the entry count of the stats is 64.
- Shipped style (`TL4_SHIPPED_STYLE`, exit 77 when unset): `tl4_strings_check` passes; stats `blocks` 5, `entries` 70,
  `max_languages` 14; for every one of the 14 ids, `get` with that explicit language succeeds for at least one string
  of each block; `locate(LOWEST)` equals `locate(explicit lowest id)`; `get` with a buffer one short fails with
  `TL4_E_BUFFER`. Counts only, no text reproduced (R-PROC-1).
- `fuzz_strings`: the tail bytes also select the language (`LOWEST` or an explicit id); promises add `locate(LOWEST) ==
  locate(lowest explicit id)` and `check` fails ⇒ stats cleared. Seeds through `mkseeds` as for the other harnesses
  (`pe/v3_languages.bin` already carries a two-language `RT_STRING` block); the same seed directory feeds Jim 2's S2.

### 9.4 Requirement wording (adds to section 6)

- **R-RES-8**: "... passes the shape check at validation **for every language entry of the block** ...; a lookup names
  the language or takes the lowest id; the core performs no language fallback."
