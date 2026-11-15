# Security requirements (mandatory design constraints)

Source: Jim 2's internal theme-parser boundary review (sections 5 and 8). These are **requirements**, not suggestions, unless a section says otherwise. Every reader, validator and getter in this repository must satisfy them before it is enabled for styles not built by us.

## A. What a theme install/load policy must enforce

Load-time rules (in uxtheme, before any parsing):

1. **Location**: the active style must be under a system-owned directory (`%SystemRoot%\Resources\Themes\…`), on a local fixed volume, reached without reparse points. No share paths, no removable media, no user-profile paths.
2. **Ownership**: the file and its directory must not be writable by ordinary users. Installing a style is then an administrator action (copy into the store), choosing among installed styles is a user action.
3. **Never as code**: styles and `shellstyle.dll` are only ever mapped as data files, as today. Keep it that way in any new loader (`LOAD_LIBRARY_AS_DATAFILE` plus image-resource flag, never a plain `LoadLibrary`).
4. **Limits**: maximum file size, maximum resource count, maximum ini/record size, maximum classes/properties, maximum image dimensions and total decoded pixels. Reject, do not truncate.
5. **Fail closed to classic**: any parse error makes the process fall back to the unthemed look, with no partial theme. A style that fails in the applying process must **not** be written to `ThemeManager` at all.
6. **Crash guard**: record "style being loaded" before parsing and clear it after; a process that finds the mark set at start-up (previous load died) skips the style. Without this a bad style locks the user out of the desktop at every logon.
7. **Metrics**: clamp every size, margin, count and font height taken from a style before it reaches `SystemParametersInfo` or any divisor.

Install-time rules (the Personalization/installer side, kept separate from DWM as the order says):

8. **Validate once, strictly, at install** with the same parser in a throwaway low-privilege process, then copy into the store. A style that does not validate is never offered.
9. `.theme` files are text that names a style, wallpaper, sounds and cursors: treat every path in them as untrusted (same location rule), and do not let a `.theme` point outside the store.
10. Third-party companion binaries from the packages (patched `uDWM.dll`, injectors, hooks) are **code**, not styles. They are out of scope for a theme policy and must never be installed by a theme installer.

Parser rules (for the existing text parser and above all for the new Windows 10 binary parser):

11. Every read is checked against the **validated** size of its resource; resource offset and size are first checked against the mapped file size (fix P8 at the source, in the resource walker, or wrap it for theme files).
12. No arithmetic on file-supplied counts or offsets without overflow checks; no division by a file-supplied value without a zero check.
13. Images: decode only the formats the format defines (32-bit BMP, PNG), check dimensions before allocation, and treat the decoders (gdiplus, win32k DIB path) as part of the audit scope before unsigned styles are allowed.
14. A fuzzing harness for the style loader (host-side, no kernel) is a precondition for accepting styles not built by us. P1–P7 are the kind of defect a first hour of fuzzing finds.

Open decision for the owner (not mine to take): whether elevated processes should refuse a per-user style entirely and use only the machine default. I recommend yes if split tokens exist or are planned.


## B. Check of 05-V4-LOADER-SKETCH.md section 3 (validation limits), 2026-11-15

Read-only check of Ryan's limits table against this note. Same labels: SOURCE where a line is cited, otherwise my assessment (HYPOTHESIS until a walker exists and is fuzzed). Nothing run.

**Verdict: the table is sound as a base (reject-not-truncate, validate before allocating, exact end of walk, per-type sizes, references resolved at load). It is not complete. Eleven additions; the first five are required before anyone implements it.**

Required:

1. **INTLIST count is too high.** The table allows `count ≤ 1,024`, but the caller's `INTLIST` holds 402 values (SOURCE `sdk/include/psdk/uxtheme.h:146-149`), and a binary getter would copy `count` values into it. The limit must be `count ≤ MAX_INTLIST_COUNT` (the corpus maximum, cbData 1,608, is count 401) and `cbData == 4 + 4*count` exactly.
2. **Signed fields.** Every record field is `int32`. Reject negative `cbData`, `classId`, `partId`, `stateId`, `nameId` and `reference` (or read them unsigned and apply the upper bounds). A negative `cbData` passes a naive `off + 32 + cbData ≤ size` test. Bound `nameId` too (1..65,535), and require `reserved == 0` since the whole corpus has it.
3. **The reference field is never an offset.** For FONT/FILENAME/DISKSTREAM it is an id that must resolve. For any other type with `reference != 0` (the "unknown table" records, open item 2 of 05) the record is dropped and counted. The value must never be used as an offset, index or pointer, and the "logical" `cbData` of a referenced record must never be used as a length.
4. **BCMAP, if it is used for the override chain.** Entry count must equal the CMAP count, each entry must be `< CMAP count` (or the "no base" value), and the chain must be **acyclic and short** (reject a cycle or a depth over, say, 8). Today the chain is at most two steps by construction; a file-supplied chain with a cycle makes every property lookup loop forever in every GUI process.
5. **The limits cannot be enforced with `FindResource`/`SizeofResource` as they are.** The row "resource offset + size must lie inside the mapped image" is right, but our resource walker does not check directory entry counts, name offsets, `OffsetToData` or `Size` (P8, SOURCE `sdk/lib/rtl/res.c:82-240`). For a data-file mapping the RVA-to-file translation also reads the section table (`e_lfanew`, `NumberOfSections`, `PointerToRawData + SizeOfRawData`) from the file. Either the walker is fixed first, or the validator carries its own small bounded PE/resource reader and hands the loader `(pointer, size)` pairs it has checked. The file-size limit must be taken from the same open handle that is mapped: a size check on the path followed by `LoadLibraryEx` on the path is a race.

Strongly suggested:

6. **Allocation limits are far above the corpus and the model is linked lists.** 4,096 classes / 65,536 part-states / 131,072 properties is 11 to 32 times the corpus maximum (374 / about 2,000 / 11,096). `MSSTYLES_AddClass` and `MSSTYLES_AddPartState` search their list linearly on every insert (SOURCE `msstyles.c:468-620`), so a style built to the limits costs billions of comparisons at every process start: a slow-logon denial of service that passes validation. Either tighten to about 4x the corpus (1,024 / 8,192 / 32,768, VARIANT 2 MB) or require the v4 builder to insert without searching (records of a class are contiguous). Also define what a duplicate (class, part, state, property) record means: first wins, and count it.
7. **Image budget is per process.** 64 Mpixels decoded per style is 256 MB of 32-bit bitmaps in every GUI process, and the image cache is only emptied when the theme file is closed (SOURCE `msstyles.c:1583-1650`). The corpus needs 6.5 Mpixels. Suggest 16 Mpixels per style, 8 Mpixels per image, and a cap on the compressed size of one image (for example 4 MB).
8. **PNG decoder exposure: the IHDR check is necessary, not sufficient.** Our path is GDI+ `Bitmap::FromStream`, which decodes through WIC and its PNG metadata readers (text, gamma, chromaticity, time, histogram: SOURCE `dll/win32/gdiplus/image.c:3507-3676`) on libpng 1.6.59 (SOURCE `sdk/include/reactos/libs/libpng/png.h:277`). The validator should walk the chunk list itself before decoding: signature, IHDR first with an allowed set (8-bit, colour type 2 or 6, or exactly what the corpus uses; no interlace unless the corpus needs it), every chunk length inside the resource, CRC checked, IEND last, and only IHDR/PLTE/tRNS/IDAT/IEND accepted. Text, ICC and other ancillary chunks have their own decompression and their own parsers, and a style has no use for them: reject or strip. Width x height x 4 computed in 64 bits. This keeps two large libraries (windowscodecs, gdiplus) on a narrow, fuzzable input. GDI+ start-up in every GUI process is also a cost to measure (HYPOTHESIS).
9. **`.mui` string loading.** `LoadStringW` with MUI redirection opens a second file, `<lang>\<style>.mui`, chosen by the user's language settings, through another loader (SOURCE: `dll/ntdll/ldr/ldrmui.c` and `dll/win32/kernel32/client/mui.c` exist; not read). That file gets none of the checks in the table. Proposal: the loader does not use MUI redirection. It reads the string table with the same bounded resource reader: from the style itself when the table is inline, otherwise from a `.mui` it opens explicitly, under the same location, ownership, size and install-time validation rules as the style. A string block is 16 length-prefixed strings: check each length against the block. Accept only the ids the records reference. `LoadStringW` truncates silently, so "reject longer than 256" cannot be detected through it. The font text then goes to the existing font parser: face name at most `LF_FACESIZE - 1` characters, height clamped.
10. **Smaller format rules.** STRING: the last character must be NUL (checked, not assumed). CMAP: both halves of an `App::Class` name non-empty; duplicate names rejected. VMAP: say whether the prefix counts bytes or characters and check it against the remaining size. Walk end: if a resource size is not a multiple of 8, state whether the walk ends at `size` or at `ALIGN8(size)`; trailing bytes that are not a whole record are a reject. Do `ALIGN8` in 64 bits and re-check `off ≤ size` after it.
11. **One validator, two callers.** Install-time and load-time validation must be the same code with the same limits, and the dropped-reference threshold must give the same answer in both, or a style can pass at install and fail (or differ) at load. With binary values (option b in 05), sizes and types are checked once at load so that no getter ever sees a value of the wrong size.

Integer-overflow summary for the walker (all untrusted 32-bit inputs): `off + 32`, `off + cbData`, `ALIGN8(off)`, `4 + 4*count`, `count * sizeof(name)`, `width * height * 4`, section `PointerToRawData + SizeOfRawData`, directory `OffsetToData + Size`. Each either in 64 bits, or as `x ≤ limit - y` with `y ≤ limit` already known.

Not checked: the corpus numbers themselves (Ryan's measurements), the BCMAP/AMAP semantics, and the WIC/libpng code.
