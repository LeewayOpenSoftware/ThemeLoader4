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
| `VMAP` / `VMAP` | 76 bytes | three length-prefixed UTF-16 strings: variant (`Normal`), size (`NormalSize`), color (`NormalColor`); the only variant in every file |
| `RMAP` / `RMAP` | ~0.5 KB | property records (1.2) at class 0: 600 display name, 601-608 documentation strings |
| `VARIANT` / `NORMAL` | 246-449 KB | the property records of the whole style (1.2) |
| `BCMAP` / `BCMAP` | int32 array | base-class map: one entry per CMAP class (HYPOTHESIS; not decoded, section 5) |
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

Walk: `off = 0; while (off + 32 <= size) { read header; off += 32; if (reference == 0) off += cbData; off = ALIGN8(off); }`. The reference field means: FONT → string id of the font string; FILENAME → IMAGE resource id; DISKSTREAM → STREAM resource id; a handful of RECT/BOOL/type-243 records → unknown table (section 5). Record order in the files: `globals` first, then classes in CMAP order, parts and states grouped; the reader must not rely on it beyond "a class's records are contiguous" (HYPOTHESIS; the current INI loader does not rely on order either).

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
7. **BCMAP** (HYPOTHESIS): one base-class index per class. Today the chain is built by name (`App::Class` → the class without app → `globals`, msstyles.c:1150-1168, the lookups in `MSSTYLES_FindPartState` msstyles.c:564-590). If `BCMAP` encodes exactly that relation it is redundant; if it encodes other bases (e.g. `DarkMode_Explorer::ListView` → `Explorer::ListView`), it is the override chain the v4 path should install. Decoding it against the known name relations is the first open item (section 5).
8. **Colour and size names**: `VMAP` gives `NormalColor`/`NormalSize`; the registry `ThemeManager` `ColorName`/`SizeName` written by the theme applier must match them (today the v3 win8 style uses `Normal Color`/`Normal Size`, VERIFIED in the lab probe's `GetCurrentThemeName` line). No other variant exists in any file, so the colour/size selection code stays trivial.
9. **Unknown types and ids**: record types 240, 241, 243 and property ids 5100-5146 do not exist in our headers; the reader must skip them by `cbData` (bounded, section 3) and count them; they must never abort the load (the stock aerolite has 801 of them). Properties with ids above `TMT_RESERVEDHIGH` 7999 other than the three atlas ids: none in the corpus.

### 2.3 What is not supported today even after a v4 reader

- Property types 212 HBITMAP, 214 STREAM, 215 BITMAPREF, 216/217 FLOAT(LIST), 218, 240, 241, 243: no getter and no `vssym32.h` symbol for 216-243 (SOURCE: `grep -c "define TMT_" sdk/include/psdk/vssym32.h` = 241 symbols, types end at 215). Of these only 240/241/243 occur in the corpus, and only in the 1809+/Win11-era files; a reader that skips them loses high-contrast colour overrides in aerolite and nothing else that is visible (HYPOTHESIS).
- Drawing features the records describe but `draw.c` does not implement are Stanley's inventory (02), not the loader's: atlas images (8000-8002), `TMT_TRANSITIONDURATIONS` 6000 / animations (`AMAP`), per-DPI image variants (`TMT_IMAGEFILE1..7` + `TMT_MINDPI1..7` are in `vssym32.h` and our `draw.c` already handles `IMAGESELECTTYPE` for v3), `DWMWindow`/`DWMPen`/`DWMTouch` classes (nothing in user mode reads them; they are the compositor's input).
- `shellstyle.dll`/DUI: out of scope for uxtheme (01 section 6).

## 3. Validation limits (Jim 2's 04 section 5, items 4 and 11-13, made concrete)

Numbers are the corpus maxima (VERIFIED over the 32 files) with headroom; the limits are the proposal, the maxima are the measurements. Everything is **reject, not truncate**, and a rejected style leaves the process unthemed (04 rule 5).

| quantity | corpus maximum | proposed limit | check |
|---|---|---|---|
| file size | 2,976,256 bytes (aero10_vista_TBMod) | 32 MB | before `LoadLibraryEx`, from the file |
| resource offset + size | — | must lie inside the mapped image, and inside the `.rsrc` section (04 P8) | every resource, before the first read |
| `PACKTHEM_VERSION` | 2 bytes | exactly 2 bytes, value 3 or 4 | as today (msstyles.c:127-138) |
| `CMAP` size / count / name length | 18,760 bytes / 374 / 54 chars | 256 KB / 4,096 names / 59 chars (`MAX_THEME_CLASS_NAME` 60 incl. the terminator, after the `::` split both halves must fit) | UTF-16 even size; each string terminated inside the resource; at most one `::` |
| `VMAP` | 76 bytes | 3 length-prefixed strings, each ≤ 64 chars, total within the resource | |
| `VARIANT` size / record count | 449,072 bytes / 11,096 | 8 MB / 131,072 | walk must end exactly at the resource end (every file in the corpus does); any record header beyond the end → reject |
| record `typeId` | 200-243 | 200 ≤ typeId ≤ 255; unknown types skipped by `cbData` | |
| record `classId` | < 374 | `< CMAP count` | |
| `partId`, `stateId` | 94, 20 | 0..255 each (the draw code indexes nothing by them, but `IsThemePartDefined` and the part tables assume small ints) | |
| `cbData` (inline) | 1,608 (INTLIST) | ≤ 65,536 and `off + 32 + cbData ≤ size` (no overflow: compute in 64 bits or check `cbData ≤ size - off - 32`) | per type: INT/BOOL/ENUM/COLOR/SIZE 4; POSITION 8; RECT/MARGINS 16; INTLIST `4 + 4*count` with `count ≤ 1,024`; STRING even and ≤ 4,096 bytes; FONT logical 92 |
| reference ids | FONT 496-507, IMAGE #508-#2125, STREAM 3 ids | the referenced resource/string must exist, else the property is dropped and counted; more than 1% dropped (or any dropped FONT) → reject (HYPOTHESIS on the threshold) | resolved at load, not at draw |
| `IMAGE`/`STREAM` count | 1,611 / 3 | 8,192 / 64 | |
| image dimensions | 1,357 x 3,720 (single image), 6.48 Mpixels total per style | ≤ 8,192 per side, ≤ 16 Mpixels per image, ≤ 64 Mpixels decoded per style (decoded on demand but counted against the budget) | from the PNG IHDR before decoding (04 rule 13); only PNG (signature check) for `IMAGE`/`STREAM`, no BMP in v4 |
| string table | font strings ≤ 40 chars | `LoadStringW` into a 256-char buffer, reject longer | |
| metrics applied to the system | fonts 9-12 pt, sizes small | clamp every size/margin/count and font height before `SystemParametersInfo` (04 rule 7); same clamps as the v3 path should get | |
| classes / part-states / properties allocated | 374 / ~2,000 / 11,096 | 4,096 / 65,536 / 131,072 | counted during the walk, before allocation |

Also from 04: load as data only (rule 3), crash mark before parsing (rule 6), fail closed (rule 5), and the install-time validation in a throwaway low-privilege process with the same parser (rule 8) are policy that applies to both the v3 and v4 paths; a fuzzing harness for the walker above (rule 14) is cheap because the walker is ~100 lines with no allocations until validation passes.

## 4. Suggested validation of the sketch itself (no implementation)

1. Decode `BCMAP` against the class-name relations (section 5, item 1) with the existing dumper; if it matches the name-derived chain, item 2.2.7 collapses.
2. Draw-by-hand check: take `Button` part 1 (PUSHBUTTON) state 1 in stock aero, resolve its `TMT_IMAGEFILE`/`IMAGECOUNT`/`SIZINGMARGINS`/`CONTENTMARGINS` records from the dump, open the referenced PNG and confirm the record values describe the image strip (VERIFIED possible on the host without running any Windows code; turns the RECT/MARGINS field order from HYPOTHESIS to VERIFIED).
3. Host reference (needs the owner's OK): `GetThemeDefaults`/`OpenThemeDataEx`+`GetThemeColor`/`GetThemeMargins` on the host for the same (class, part, state, property) tuples to compare with the dump values.
4. Jim 2 reviews section 3 against 04; Stanley maps 2.3 onto 02's feature table.

## 5. Open items

1. `BCMAP` semantics (int32 per class; HYPOTHESIS base-class index).
2. The reference field on 10 RECT + 1 BOOL records (stock aero) and 12 type-243 records (aerolite): which table they point into.
3. Types 240/241/243 and property ids 5100-5146 (high-contrast / Win11 additions, HYPOTHESIS).
4. `AMAP`, `PVL`, `IMMERSIVE`, `DESKTOP` contents (not needed to draw static parts).
5. The signature trailer policy (01 section 2.6; Jim 2's 04 section 4): the v4 reader must not depend on it, the install policy decides.
6. Whether `MSSTYLES_LoadBitmap`'s name cache or a parallel id cache is used for v4 images (implementation detail flagged for the reviewer).

## 6. Evidence

`research-dwm/ryan/pe-dump.json` (all per-file tables), `ryan/class-matrix.txt`, `ryan/vsprobe-cand3-6f817ad0bf7.log`; corpus maxima computed by a one-off pass over the 32 files with the same dumper logic (walk + PNG IHDR), 2026-11-15. Source lines: `dll/win32/uxtheme/msstyles.c`, `msstyles.h`, `system.c`, `property.c`, `stylemap.c`, `uxini.c`, `sdk/include/psdk/vssym32.h` at cand3 6f817ad0bf7.
