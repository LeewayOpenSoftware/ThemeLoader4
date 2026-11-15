# 05 — PACKTHEM_VERSION 4 loader sketch

Follow-up to `01-THEME-INPUTS-AND-MATRIX.md` (Ryan, 2026-11-15), requested by god after 01. **Research only: this is a design sketch, not an implementation plan with a schedule and nothing here is code.** Labels as in 01: VERIFIED (observed in the files or the lab), SOURCE (path:line, tree cand3 6f817ad0bf7), HYPOTHESIS.

## 1. What the reader has to parse

Every statement in this section is VERIFIED on all 32 supplied styles (ryan/pe-dump.json, every VARIANT walked to its last byte with zero bad records) unless marked.

### 1.1 Container

A resource-only PE mapped with `LOAD_LIBRARY_AS_DATAFILE` (keep it that way; Jim 2's rule 3). The reader uses `FindResourceW`/`SizeofResource`/`LoadResource` exactly like the version-3 path, but with these resources:

| resource (type / name) | size seen | what the reader takes from it |
|---|---|---|
| `PACKTHEM_VERSION` / #1 | 2 bytes = 4 | selects the v4 path (3 keeps the INI path) |
| `CMAP` / `CMAP` | 11-19 KB | UTF-16 class names, each zero-terminated and padded to 8 bytes; the ordinal of the n-th non-empty string is the `classId` used by the records. 252-374 names; longest 54 chars; 152-270 of them are `App::Class` names (one `::` only) |
| `VMAP` (`VMAP`) | 76 bytes | three strings, each a 32-bit prefix counting UTF-16 **characters including the terminating NUL**, then the UTF-16 text, then padding to a 4-byte boundary (VERIFIED on the 32 files: 7 `Normal`, 11 `NormalSize`, 12 `NormalColor` = 20 + 28 + 28 = 76 bytes): variant, size, color; the only variant in every file (R-V4-3) |
| `RMAP` / `RMAP` | ~0.5 KB | property records (1.2) at class 0: 600 display name, 601-608 documentation strings |
| `VARIANT` / `NORMAL` | 246-449 KB | the property records of the whole style (1.2) |
| `BCMAP` (`BCMAP`) | 4 + 4 x N bytes | **VERIFIED** (32 files): a uint32 count N followed by N uint32 entries, N = CMAP count minus the leading descriptor names (`documentation`, `sizevariant.*`, `colorvariant.*`: 3 or 4). Entry j belongs to the class with CMAP ordinal j + (CMAP count - N); its value is `0xFFFFFFFF` (no base) or the base class in the same shifted ordinal space (base ordinal = value + CMAP count - N). Bases are `App::Class` -> `Class`, `Class` -> `ClassStyle`, `DarkMode_X::C` -> `X::C` and the like. No cycle in any file; longest chain 6 (limit 8, R-V4-10) |
| `AMAP` / `AMAP` | binary | animations (HYPOTHESIS; not needed to draw static parts) |
| `IMAGE` / #508… | 567-1611 PNGs | part images, by numeric id |
| `STREAM` / 3 ids | 57-345 KB PNGs | atlas images, by numeric id |
| string table (`#6`) inline, or `MUI` + `<lang>\<file>.mui` | | font strings `"Segoe UI, 9, Quality:ClearType"` at ids 496-507 and the documentation strings, read with `LoadStringW`-class access on the module (MUI redirection handles the stock split; the third-party files are self-contained) |
| `PVL`, `IMMERSIVE`, `DESKTOP` (#1), `MINCOLORDEPTH` | 4, 4, 4, 2 bytes | not needed to draw (HYPOTHESIS); `MINCOLORDEPTH` = 15 as in v3 |

Absent: everything the v3 path needs (`COLORNAMES`, `SIZENAMES`, `FILERESNAMES`, `TEXTFILE`, `BITMAP` images). The v4 reader synthesizes the colour/size name lists from `VMAP` so that `EnumThemeColors`/`EnumThemeSizes`/`GetThemeDefaults` and the `ThemeManager` `ColorName`/`SizeName` registry values keep working (`.theme` files already say `ColorStyle=NormalColor`, `Size=NormalSize`).

### 1.2 Property records

```
struct V4Record {           // 32 bytes, records are 8-byte aligned
    int32 nameId;           // TMT_* property id
    int32 typeId;           // primitive type, see 1.3
    int32 classId;          // index into the CMAP name list
    int32 partId;           // 0..94 seen
    int32 stateId;          // 0..20 seen
    int32 reference;        // 0 -> cbData bytes of inline data follow; != 0 -> no inline data, see below
    int32 reserved;         // always 0 in the corpus
    int32 cbData;           // inline size (reference == 0) or logical size (reference != 0; 92 for FONT)
};
```

Walk: `off = 0; while (off + 32 <= size) { read header; off += 32; if (reference == 0) off += cbData; off = ALIGN8(off); }` and the walk must end exactly at `size`: every `VARIANT` and every `RMAP` in the 32 files has a size that is a multiple of 8 and is consumed to the last byte (VERIFIED), so leftover bytes are a reject (R-V4-4). The reference field means: FONT -> string id of the font string; FILENAME -> IMAGE resource id; DISKSTREAM -> STREAM resource id; a handful of RECT/BOOL/type-243 records -> unknown table (section 5); it is never an offset, index or length (R-V4-7). Record order in the files: `globals` first, then classes in ascending CMAP order (VERIFIED for stock aero: the first record of each class appears in ascending class id), parts and states grouped; the reader must not rely on it (the current INI loader does not rely on order either).

### 1.3 Primitive types in the corpus and the inline encodings

| typeId | TMT name | inline data (reference == 0) | seen in corpus |
|---|---|---|---|
| 200 | ENUM | int32 | yes (1,184 in stock aero) |
| 201 | STRING | UTF-16, cbData bytes, zero-terminated | yes (2 per file + RMAP) |
| 202 | INT | int32 | yes |
| 203 | BOOL | int32 (0/1) | yes |
| 204 | COLOR | COLORREF as int32 (0x00BBGGRR) | yes |
| 205 | MARGINS | 4 x int32 (left, right, top, bottom) | yes |
| 206 | FILENAME | none; reference = IMAGE id | yes (2,133 in stock aero) |
| 207 | SIZE | int32 | yes |
| 208 | POSITION | 2 x int32 | yes |
| 209 | RECT | 4 x int32 | yes (10 header-only in stock aero) |
| 210 | FONT | none; reference = string id; cbData 92 | yes |
| 211 | INTLIST | int32 count + count x int32 (cbData covers them; max cbData 1,608 in the corpus) | yes |
| 213 | DISKSTREAM | none; reference = STREAM id | yes (3 per file) |
| 212, 214, 215 | HBITMAP, STREAM, BITMAPREF | — | not seen |
| 216, 217 | FLOAT, FLOATLIST (`vssym32.h` has no defines for them) | — | not seen |
| 218 | HIGHCONTRASTCOLORTYPE | — | not seen |
| 240 | (`SIMPLIFIEDIMAGETYPE` in 01's naming; HYPOTHESIS) | int32 | aerolite only (property ids 5100-5105) |
| 241 | unknown, colour-like 4 bytes | int32 | 1809+/Win11-era files; 801 records in aerolite under ids 5110-5122 |
| 243 | unknown | header-only | aerolite only, ids 5144-5146 |

Inline encodings for ENUM/INT/BOOL/COLOR/SIZE/POSITION/RECT/MARGINS/INTLIST are VERIFIED by sampling values (e.g. `globals` colours 3804/3805 as COLORREFs, record 2401 MINCOLORDEPTH = 15); the RECT/MARGINS field order is the Windows `RECT`/`MARGINS` order (HYPOTHESIS until a part is drawn and compared).

## 2. How this maps onto our uxtheme model

### 2.1 The model today (SOURCE)

`THEME_FILE` → list of `THEME_CLASS` (`szAppName[60]`, `szClassName[60]`, `partstate` list, `overrides`) → `THEME_PARTSTATE` (`iPartId`, `iStateId`, `properties`) → `THEME_PROPERTY` (`iPrimitiveType`, `iPropertyId`, `origin`, `lpValue`, `dwValueLen`) (msstyles.h). Metrics (`[SysMetrics]`) are a separate `THEME_PROPERTY` list on the file (`tf->metrics`, `MSSTYLES_AddMetric` msstyles.c:813). Images are a `THEME_IMAGE` cache keyed by **name** (`WCHAR name[MAX_PATH]`, `MSSTYLES_LoadBitmap` msstyles.c:1583-1650, the file name's `\ / .` turned into `_` and looked up as an `IMAGE` PNG then a `BITMAP`).

The decisive point: **`THEME_PROPERTY` holds the value as INI text** (`lpValue` points into the TEXTFILE, `dwValueLen` chars) and every getter re-parses that text on each call: `MSSTYLES_GetPropertyColor` reads three integers (msstyles.c:1736-1760), `GetPropertyInt`/`Bool`/`Position`/`Rect`/`Margins`/`IntList`/`String`/`Font` likewise (msstyles.c:1723-1915); enums are resolved by name through `MSSTYLES_LookupEnum` (property.c:85 → stylemap.c `mapEnum`). The INI parser is `MSSTYLES_ParseThemeIni` (msstyles.c:1057-1172): `[SysMetrics]` → metrics (+ `SystemParametersInfo` when applying), `[App::Class.Part(State)]` sections → `MSSTYLES_AddClass`/`AddPartState`/`AddProperty` with `isGlobal` for the `globals` class; after parsing, the `overrides` chain is built (msstyles.c:1150-1168) and `MSSTYLES_FindProperty`/`FindPartState` walk state → part → class → overrides (msstyles.c:564-590, 1421-1450).

### 2.2 Where the v4 path plugs in (sketch, HYPOTHESIS as design)

1. `MSSTYLES_OpenThemeFile` (msstyles.c:99): after the version resource is read, branch on `version == 4`: validate the v4 resources (section 3), build `pszAvailColors`/`pszAvailSizes` from `VMAP`, mark the `THEME_FILE` as v4. Version 3 keeps the current code unchanged. Any other version stays `ERROR_BAD_FORMAT`.
2. `MSSTYLES_ParseThemeIni` callers (msstyles.c:274, 292, 1347 → `MSSTYLES_SetActiveTheme`, `OpenThemeClassFromFile`): for a v4 file call a `MSSTYLES_ParseV4` that produces the same `THEME_CLASS`/`THEME_PARTSTATE`/`THEME_PROPERTY` lists:
   - class name `App::Class` → split at the single `::` into `szAppName`/`szClassName` (54 chars max in the corpus, fits the 60-char fields; `MSSTYLES_ParseIniSectionName` msstyles.c:389 does the same split for INI section names); a name without `::` → empty app name; `globals` → `isGlobal`; `sysmetrics` → the metrics list (the v3 `[SysMetrics]` section) with the same colour/font/size application when `setMetrics`.
   - (classId, partId, stateId) → `MSSTYLES_AddClass` + `MSSTYLES_AddPartState`; the record's `nameId`/`typeId` go straight into `iPropertyId`/`iPrimitiveType` (no `MSSTYLES_LookupProperty` by name; `stylemap.c` is then only needed for the INI path and for `GetThemeEnumValue` by name).
   - `origin` follows the existing rule in `MSSTYLES_AddProperty` (state > part > global > class).
3. **Values**: two options, to be decided by the implementer, not here:
   - (a) keep `lpValue` text and synthesize INI text for every record at load (`"255 0 255"`, `"1"`, `"IMAGE508"`…). Zero getter changes, but it allocates strings for ~11,000 records per process and turns binary data into text only to parse it back; FILENAME and FONT would need synthetic names anyway.
   - (b) add a binary representation to `THEME_PROPERTY` (a `const BYTE *lpData; DWORD cbData; int reference;` beside `lpValue`, or a tagged union) and make each `MSSTYLES_GetProperty*` return the binary value when present. Nine getters change, each by one `if`. INTLIST, COLOR, RECT, MARGINS, POSITION, SIZE, INT, BOOL, ENUM are direct copies; STRING is a copy of the UTF-16.
   (b) is the natural fit and is what the rest of this sketch assumes.
4. **FONT**: `MSSTYLES_GetPropertyFont` (msstyles.c:1795) parses `"name, size, Bold, Italic, Underline, Quality:ClearType"`-style text today; for v4 it loads string `reference` from the module (`LoadStringW(hTheme, id)`, which follows MUI for the stock files and reads the inline `#6` table otherwise) and feeds that text to the existing parser. The strings in the corpus are exactly in the syntax the v3 INI uses (VERIFIED: `Segoe UI, 9, Quality:ClearType`, `Segoe UI, 9, Bold, Quality:ClearType`, `Segoe UI, 12, Underline, Quality:ClearType`).
5. **FILENAME / IMAGE**: `MSSTYLES_LoadBitmap` keys the cache by name; a v4 FILENAME record gives an IMAGE **id**. Minimal change: a second lookup path by `MAKEINTRESOURCEW(reference)` on type `IMAGE` into the same PNG loader (`MSSTYLES_TryLoadPng` msstyles.c:1622, `pngsup.cpp`), cache key = synthetic name `#<id>` or an id field in `THEME_IMAGE`. The draw code (`draw.c`) asks for `TMT_IMAGEFILE`, `TMT_IMAGEFILE1..7`, `TMT_GLYPHIMAGEFILE` through `MSSTYLES_FindProperty(TMT_FILENAME, id)` then `MSSTYLES_LoadBitmap(tc, text)`; with (b) it passes the reference instead.
6. **DISKSTREAM / STREAM**: the three atlases are PNGs too; same PNG loader on type `STREAM`. Which properties reference them (`TMT_ATLASIMAGE` 8000, `TMT_ATLASINPUTIMAGE` 8001, `TMT_ATLASRECT` 8002 exist in our `vssym32.h`) and how `draw.c` would use an atlas rectangle is new drawing work, not loader work; the loader only has to resolve the id.
7. **BCMAP** (VERIFIED, section 1.1): one base-class ordinal per class in the shifted ordinal space, no base = `0xFFFFFFFF`. It encodes more than the name relation the INI loader derives (`App::Class` -> `Class` -> `globals`, msstyles.c:1150-1168): `Class` -> `ClassStyle` (`Button` -> `ButtonStyle`, `Edit` -> `EditStyle`, `ControlPanel` -> `ControlPanelStyle`), `DarkMode_X::C` -> `X::C`, and chains up to 6 long. The v4 path installs this chain as the override list and `MSSTYLES_FindPartState` walks it; R-V4-10's acyclicity and depth check (8) hold on every reference file.
8. **Colour and size names**: `VMAP` gives `NormalColor`/`NormalSize`; the registry `ThemeManager` `ColorName`/`SizeName` written by the theme applier must match them (today the v3 win8 style uses `Normal Color`/`Normal Size`, VERIFIED in the lab probe's `GetCurrentThemeName` line). No other variant exists in any file, so the colour/size selection code stays trivial.
9. **Unknown types and ids**: record types 240, 241, 243 and property ids 5100-5146 do not exist in our headers; the reader must skip them by `cbData` (bounded, section 3) and count them; they must never abort the load (the stock aerolite has 801 of them). Properties with ids above `TMT_RESERVEDHIGH` 7999 other than the three atlas ids: none in the corpus.

### 2.3 What is not supported today even after a v4 reader

- Property types 212 HBITMAP, 214 STREAM, 215 BITMAPREF, 216/217 FLOAT(LIST), 218, 240, 241, 243: no getter and no `vssym32.h` symbol for 216-243 (SOURCE: `grep -c "define TMT_" sdk/include/psdk/vssym32.h` = 241 symbols, types end at 215). Of these only 240/241/243 occur in the corpus, and only in the 1809+/Win11-era files; a reader that skips them loses high-contrast colour overrides in aerolite and nothing else that is visible (HYPOTHESIS).
- Drawing features the records describe but `draw.c` does not implement are Stanley's inventory (02), not the loader's: atlas images (8000-8002), `TMT_TRANSITIONDURATIONS` 6000 / animations (`AMAP`), per-DPI image variants (`TMT_IMAGEFILE1..7` + `TMT_MINDPI1..7` are in `vssym32.h` and our `draw.c` already handles `IMAGESELECTTYPE` for v3), `DWMWindow`/`DWMPen`/`DWMTouch` classes (nothing in user mode reads them; they are the compositor's input).
- `shellstyle.dll`/DUI: out of scope for uxtheme (01 section 6).

## 3. Validation limits

**Normative limits live in `docs/security/REQUIREMENTS.md` section 6 and win over this table** (owner order 2026-11-15). The table keeps the corpus maxima (VERIFIED over the 32 files) and quotes the normative limit next to each; the earlier proposals that REQUIREMENTS tightened are gone. Everything is **reject, not truncate**, and a rejected style leaves the process unthemed (R-POL-4).

| quantity | corpus maximum | normative limit (REQUIREMENTS section 6) | check |
|---|---|---|---|
| file size | 2,976,256 bytes (aero10_vista_TBMod) | 32 MB | from the mapping handle (R-POL-2); sections 1..32 (corpus 4) |
| resource offset + size | — | must lie inside the mapped image, and inside the `.rsrc` section (04 P8) | every resource, before the first read |
| `PACKTHEM_VERSION` | 2 bytes | exactly 2 bytes, value 3 or 4 | as today (msstyles.c:127-138) |
| `CMAP` size / count / name length | 18,760 bytes / 374 / 54 chars | 256 KB / 1,024 names / 59 chars per half (`MAX_THEME_CLASS_NAME` 60 incl. the terminator) | UTF-16 even size; each string terminated inside the resource; at most one `::`, both halves non-empty; duplicates rejected (R-V4-2) |
| `VMAP` | 76 bytes | 3 strings, each <= 64 chars; prefix = characters incl. NUL, 4-byte padded (section 1.1) | R-V4-3 |
| `VARIANT` size / record count | 449,072 bytes / 11,096 | 2 MB / 32,768 | walk ends exactly at the resource size (every file in the corpus does; sizes are multiples of 8); leftover bytes -> reject (R-V4-4) |
| record `typeId` | 200-243 | 200 <= typeId <= 255; unknown types skipped by their checked `cbData`, dropped and counted (R-V4-8) | all header fields read unsigned and range-checked; `reserved == 0`; `nameId` 1..65,535 (R-V4-5, R-GEN-4) |
| record `classId` | < 374 | `< CMAP count` | |
| `partId`, `stateId` | 94, 20 | 0..255 each (the draw code indexes nothing by them, but `IsThemePartDefined` and the part tables assume small ints) | |
| `cbData` (inline) | 1,608 (INTLIST, count 401) | <= 65,536 and `off + 32 + cbData <= size` in 64 bits | exact per type: INT/BOOL/ENUM/COLOR/SIZE 4; POSITION 8; RECT/MARGINS 16; INTLIST `4 + 4*count` with `count <= 402` (the caller's `INTLIST` capacity); STRING even, <= 4,096 bytes, last character NUL; FONT logical 92 (R-V4-6) |
| reference ids | FONT 496-507, IMAGE #508-#2125, STREAM 3 ids | an identifier only, resolved through the bounded reader; unresolved -> the property is dropped and counted; more than 1% dropped, or any dropped FONT -> reject (R-V4-7, R-V4-12) | resolved at load, not at draw |
| `IMAGE`/`STREAM` count | 1,611 / 3 | 4,096 / 16 | |
| image dimensions | 1,357 x 3,720 (single image), 6.48 Mpixels total per style | <= 8,192 per side, <= 8 Mpixels per image, <= 4 MB compressed, <= 16 Mpixels decoded per style per process | from the PNG IHDR before decoding (R-RES-4); PNG only (R-RES-5). IHDR set VERIFIED on all 30,171 images of the 32 files: bit depth 8, colour type 6 (RGBA, 26,729) or 2 (RGB, 3,442), compression 0, filter 0, interlace 0, which is exactly R-RES-3's allowed set |
| string table | font strings <= 40 chars | read with the bounded reader, no MUI redirection; a font string <= 255 chars, face <= 31 (R-RES-1, R-RES-2) | |
| metrics applied to the system | fonts 9-12 pt, sizes small | clamp every size/margin/count and font height before `SystemParametersInfo` (04 rule 7); same clamps as the v3 path should get | |
| classes / part-states / properties allocated | 374 / ~2,000 / 11,096 | 1,024 / 8,192 / 32,768 | counted during the walk, before allocation (R-GEN-7); the model builder must not search a list per record (R-MOD-1) |

Also from 04: load as data only (rule 3), crash mark before parsing (rule 6), fail closed (rule 5), and the install-time validation in a throwaway low-privilege process with the same parser (rule 8) are policy that applies to both the v3 and v4 paths; a fuzzing harness for the walker above (rule 14) is cheap because the walker is ~100 lines with no allocations until validation passes.

## 4. Suggested validation of the sketch itself (no implementation)

1. ~~Decode `BCMAP`~~ done (section 1.1): it is the override chain, richer than the name relation, acyclic, depth <= 6.
2. Draw-by-hand check: take `Button` part 1 (PUSHBUTTON) state 1 in stock aero, resolve its `TMT_IMAGEFILE`/`IMAGECOUNT`/`SIZINGMARGINS`/`CONTENTMARGINS` records from the dump, open the referenced PNG and confirm the record values describe the image strip (VERIFIED possible on the host without running any Windows code; turns the RECT/MARGINS field order from HYPOTHESIS to VERIFIED).
3. Host reference (needs the owner's OK): `GetThemeDefaults`/`OpenThemeDataEx`+`GetThemeColor`/`GetThemeMargins` on the host for the same (class, part, state, property) tuples to compare with the dump values.
4. Jim 2 reviews section 3 against 04; Stanley maps 2.3 onto 02's feature table.

## 5. Open items

1. ~~`BCMAP` semantics~~ resolved (section 1.1, VERIFIED on the 32 files).
2. The reference field on 10 RECT + 1 BOOL records (stock aero) and 12 type-243 records (aerolite): which table they point into.
3. Types 240/241/243 and property ids 5100-5146 (high-contrast / Win11 additions, HYPOTHESIS).
4. `AMAP`, `PVL`, `IMMERSIVE`, `DESKTOP` contents (not needed to draw static parts).
5. The signature trailer policy (01 section 2.6; Jim 2's 04 section 4): the v4 reader must not depend on it, the install policy decides.
6. Whether `MSSTYLES_LoadBitmap`'s name cache or a parallel id cache is used for v4 images (implementation detail flagged for the reviewer).
7. Resource-directory ordering and languages (R-PE-8), measured on all 59 PE files of the inputs (32 styles, 11 shellstyle DLLs, 16 .mui): 0 ordering violations (names before ids, both strictly ascending), 0 styles with more than one language entry per (type, name); one third-party `shellstyle.dll` (Aero11 Seven) carries two language entries for `UIFILE` #1 and would be rejected by R-PE-8. Maximum entries in one directory 1,611, longest name 20 characters, at most 4 sections. The L0 reader enforces the rule as written.

## 6. Evidence

`research-dwm/ryan/pe-dump.json` (all per-file tables), `ryan/class-matrix.txt`, `ryan/vsprobe-cand3-6f817ad0bf7.log`; corpus maxima computed by a one-off pass over the 32 files with the same dumper logic (walk + PNG IHDR), 2026-11-15. Source lines: `dll/win32/uxtheme/msstyles.c`, `msstyles.h`, `system.c`, `property.c`, `stylemap.c`, `uxini.c`, `sdk/include/psdk/vssym32.h` at cand3 6f817ad0bf7.
