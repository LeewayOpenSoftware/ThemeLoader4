# I1: integration of the version-4 loader into LiberNT uxtheme (design)

Author: Stanley (Leeway Open Software), 2026-11-15. Status: **design for review by Jim 2**. No implementation starts before his OK.

**Authority:** owner decisions of 2026-11-15:
- (1) R2/I1 authorized: v3 stays operational; v4 is added incrementally behind a controlled boundary, with regression tests and rollback; Jim 2's requirements are mandatory; no wholesale rewrite of uxtheme, Explorer or DWM.
- (2) Windows reference testing authorized in a controlled Windows environment.
- (3) Privileged processes never load theme resources from user-writable locations.
- (4) No GitHub spending increase; Lab01 is evaluated as a local machine.

**Inputs:**
- `docs/design/V4-LOADER-SKETCH.md` (05), section 2.2;
- `docs/design/L2-MODEL-DESIGN.md`;
- `docs/security/REQUIREMENTS.md` (normative);
- LiberNT research `research-dwm/02-OROSI-PIPELINE-INVENTORY.md`, `04-THEME-PARSER-BOUNDARY.md`, `REPORT-DWM-VISUAL-STYLES.md`.

**Labels:**
- **SOURCE:** path:line. TL4 paths at main d78b38d; LiberNT paths at `leeway/main` ad21954.
- **HYPOTHESIS:** an expectation, to be measured.
- **DECISION:** a choice this design makes, open to review.

## 0. Gates and what this document does not do

- **Non-goals:** no rewrite of uxtheme, Explorer or DWM; no frame painter (R5 of the report, a separate milestone); no change of the v3 parser beyond the hooks listed in section 2; no DirectUI integration.
- **R-PROC-3 says "integration not before S2".** The owner authorized design **and initial implementation** now, with the remaining validation before production use.

  DECISION, for Jim 2 to confirm:
  1. I1a code may be written and merged **compiled out by default** (section 4) after this design is approved.
  2. The build option may be turned on in lab builds after the S1 report.
  3. The runtime switch may be offered to anyone outside the lab only after S2 and the R-MOD-7 clamp table.
- **R-MOD-7:** the clamp table is added to REQUIREMENTS.md **before** the adapter code that applies metrics. Until then I1a applies no metric from a v4 style to the system (section 6).

## 1. Where uxtheme stands today (SOURCE)

- **Open:** `MSSTYLES_OpenThemeFile` (`dll/win32/uxtheme/msstyles.c:99`):
  - maps the style with `LoadLibraryExW(..., LOAD_LIBRARY_AS_DATAFILE)` (`:115`);
  - requires `PACKTHEM_VERSION` = 3 (`:122-134`);
  - reads the INI resources.
- **Model:** `THEME_FILE` → `THEME_CLASS` list → `THEME_PARTSTATE` list → `THEME_PROPERTY` (`msstyles.h`).
  - A property value is INI text (`lpValue`, `dwValueLen`), re-parsed by the getters `MSSTYLES_GetProperty*` (`msstyles.c:1723-1915`).
  - Lookups go through `MSSTYLES_FindProperty` (`:1421`), `MSSTYLES_FindPart`/`FindPartState` (`:532,564`), `MSSTYLES_FindMetric` (`:745`) and `MSSTYLES_FindAppClass` (`:1206`).
- **Consumers outside `msstyles.c` that see the representation:**
  - `draw.c:268,326` copies `lpValue` as an image file name;
  - `ncscrollbar.c:131` and `system.c:1120` ask for part-states;
  - `metric.c:49-238` reads `tf->metrics`;
  - `system.c:1171` reads INI documentation values.
- **Images:** `MSSTYLES_LoadBitmap` (`msstyles.c:1583-1660`) looks resources up by name (PNG through `pngsup.cpp`, else BMP).
- **Settings:** the active style comes from `HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager` (`DllName`, `ColorName`, `SizeName`) (`system.c:149-200`), with no location or owner check (04 section 2).

## 2. The boundary

### 2.1 Shape

**DECISION: delegate, do not convert.** A v4 style is not turned into the v3 linked lists.

- The `THEME_FILE` of a v4 style holds the TL4 model.
- Each `THEME_CLASS` of a v4 style holds a class ordinal.
- Lookups call `tl4_model_find` (TL4 `loader/include/tl4/loader/model.h`), which already mirrors `MSSTYLES_FindProperty`'s search order (R-MOD-6), using the BCMAP base chain instead of uxtheme's name-built `overrides`.

Reasons:
- one lookup implementation for v4, the one that is fuzzed;
- no second copy of 11,000 records per process;
- the v3 lists and their code stay untouched.

New code lives in one new file, `dll/win32/uxtheme/msstyles_v4.c` (the adapter). The only edits to existing files are dispatch points, listed in 2.4, each one `if (tf->v4) return MSSTYLES_V4_...;` placed **before** the v3 code. They sit under `#ifdef UXTHEME_V4` (section 4).

### 2.2 Data structures (added fields, v3 values unchanged)

| Struct | Added | Meaning |
|---|---|---|
| `THEME_FILE` | `struct _V4_FILE *v4` | NULL for v3. Otherwise holds: the file and section handles and the read-only view (R-POL-2); the `.mui` view; the `tl4_model` and its buffer; the per-file pixel budget (R-RES-4); and the `THEME_PROPERTY` shadow array (below). |
| `THEME_CLASS` | `UINT v4Ordinal` | the TL4 class ordinal; unused for v3 |
| `THEME_PROPERTY` | `const tl4_prop *v4` | NULL for v3. For v4, `lpValue` = NULL and `dwValueLen` = 0; `iPrimitiveType`, `iPropertyId` and `origin` are filled from the `tl4_prop` |
| `THEME_IMAGE` | `UINT v4Id` | IMAGE/STREAM id for v4; `name` stays empty |

**Shadow array:**
- At load, the adapter allocates one `THEME_PROPERTY` per `tl4_prop` (`model.nprops`, at most 32,768 by REQUIREMENTS section 6) and fills them.
- `MSSTYLES_FindProperty` on a v4 class returns `&shadow[p - model.props]`.
- The pointer is stable for the life of the file and read-only after load, so the existing `PTHEME_PROPERTY` contract (returned pointer, no ownership) holds.

Size per process (HYPOTHESIS, to be measured in I1a): about 0.5 MB model plus about 0.5 MB shadow at corpus maxima.

### 2.3 Load sequence (v4 branch of `MSSTYLES_OpenThemeFile`)

1. **Switch** (section 4). Off: the current code runs unchanged from its first line.
2. **Location and trust** (section 5): resolve and check the path before any byte is read.
3. **Map the file.**
   - `CreateFileW(GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT)`; reject a reparse point.
   - `GetFileSizeEx` on that handle, then a read-only section and view of exactly that size (R-POL-2). The size limit (32 MB, R-PE) is checked before mapping.
   - Write sharing is denied, but R-GEN-11 still holds: the core never assumes stable bytes.
4. **Version peek.** `tl4_pe_open` + `tl4_pe_find(PACKTHEM_VERSION)`. If 3: unmap, close, and continue with the **existing** `LoadLibraryExW` path, unchanged. If 4: continue. Anything else: `ERROR_BAD_FORMAT`, as today.
5. **Strings.**
   - Open `<style dir>\<lang>\<style name>.mui` explicitly (R-RES-1). `lang` is the user UI language, then `en-US`.
   - The `.mui` passes the same location check (step 2) and mapping (step 3).
   - No MUI redirection and no `LoadStringW` on the style.
6. **Validate and build.** `tl4_model_size(stats)` from `tl4_v4_validate`, then allocate the 8-byte-aligned buffer and call `tl4_model_build` (R-MOD-5; one validator, R-GEN-10). Then check that the unresolved references are within R-V4-12.
7. **Variants.**
   - `pszAvailColors`/`pszAvailSizes` come from `VMAP` (corpus: `NormalColor`/`NormalSize`).
   - A requested colour or size name that VMAP does not list is `E_PROP_ID_UNSUPPORTED`, as v3 does today.
8. **Shadow array, crash-guard clear (5.4), return.**

Any failure in steps 2–8: free everything, set no partial state, and return the error. The caller falls back to classic (R-POL-4, section 7).

### 2.4 Dispatch points

| Existing function (SOURCE) | v4 behaviour |
|---|---|
| `MSSTYLES_FindAppClass` `msstyles.c:1206` | build `App::Class` (UTF-16), then `Class`, and look each up with `tl4_model_class`; the 120-unit limit is TL4's (`TL4_MODEL_MAX_CLASS_QUERY`); a longer name means not found |
| `MSSTYLES_FindProperty` `:1421` | `tl4_model_find(ordinal, part, state, id)` → shadow entry. The primitive-type argument is checked against `tl4_prop.type`; a mismatch means not found, as the v3 list filter does |
| `MSSTYLES_FindPart` / `FindPartState` `:532,564` (used by `system.c:1120` `IsThemePartDefined`, `ncscrollbar.c:131`) | `tl4_model_part_defined`; `FindPartState` returns a static sentinel `THEME_PARTSTATE` for v4 (callers test non-NULL only; to be confirmed for every caller in I1a) |
| `MSSTYLES_FindMetric` `:745`, `metric.c` | I1a: not found for v4, so the system values stay. I1b: the `sysmetrics` class through the clamp table (section 6) |
| `MSSTYLES_GetPropertyBool/Color/Int/IntList/Position/Rect/Margins/String/Font` `:1723-1915` | `if (tp->v4)` → the matching `tl4_get_*`. FONT: `tl4_get_font` → `LOGFONTW` (height from points at the class DPI, as the v3 code does). Every TL4 error → `E_PROP_ID_UNSUPPORTED` |
| `draw.c:268,326` (file name from `lpValue`) | `tp->v4` → `MSSTYLES_V4_LoadImage(tc, tp)` instead of the name path |
| `MSSTYLES_LoadBitmap` `:1583` | unchanged for v3; v4 uses `MSSTYLES_V4_LoadImage` (2.5) |
| `GetThemeDocumentationProperty` (`system.c:1171`, INI) | `tl4_model_find_rmap` on the RMAP area (strings 600–608) |
| `CheckThemeSignature`, `GetThemeDefaults` | open/close through the same path; no signature semantics (REQUIREMENTS section 1) |

`MSSTYLES_CloseThemeFile` (`:217`) frees the v4 part: model buffer, shadow array, views, handles and images.

### 2.5 Images (R-RES-3..7)

`MSSTYLES_V4_LoadImage`, in order:
1. `tl4_get_filename` gives (id, offset, size).
2. Check the size against R-RES limits, then make a **private copy** of exactly `size` bytes (R-RES-6).
3. `tl4_png_check` on the copy, then `tl4_pixel_budget_take` (16 M pixels per style per process, R-RES-4).
4. Decode the copy with the existing PNG path (`pngsup.cpp`, GDI+) **from memory**.
5. Compare the decoded size with the walker's.
6. Create the HBITMAP and cache it in `THEME_IMAGE` keyed by `v4Id`.

Any failure: the property counts as not found, and the part draws as the v3 code does when an image is missing. DIB alpha handling is the v3 code's (`hasAlpha`).

## 3. Build model

**DECISION: static library, vendored at a pinned commit.**

- **Location.** TL4's `loader/` core (C11, no OS calls, R-GEN-1) is imported into LiberNT as `sdk/lib/tl4/`. It is a copy of `loader/include` and `loader/src` only: no tests, fuzzers or docs. A `TL4_COMMIT` file names the exact TL4 main commit.
- **Linking.** It is linked **statically** into `uxtheme.dll` and into the install-time checker (5.3).
- **Why not a DLL:** no new module loaded into every GUI process, no export ABI to version, and one validator per build, guaranteed by the link (R-GEN-10).
- **TL4 stays host-portable.** All Windows code (file mapping, tokens, ACLs, GDI+, registry) lives in the LiberNT adapter, never in TL4.
- **Updates.** A TL4 change reaches LiberNT only as a commit that replaces `sdk/lib/tl4/` and `TL4_COMMIT` together. TL4's tests run against that same commit (Lab01/libernt-srv, results traceable to the commit; owner decision 4).
- **Toolchains.** The core must build warning-free with every LiberNT toolchain (GCC/Clang and MSVC, R-GEN-9). TL4's Windows CI job has never run (billing). DECISION: an MSVC build of the TL4 core on Lab01 or the private runner is an I1a entry criterion.

## 4. The switch

Two gates; both must be on:

| Gate | Where | Default | Effect when off |
|---|---|---|---|
| Build | CMake option `UXTHEME_V4` (`dll/win32/uxtheme/CMakeLists.txt`), which defines `UXTHEME_V4` and links `sdk/lib/tl4` | **OFF** | Every adapter hook is `#ifdef`-ed out. `uxtheme.dll` builds from the same preprocessed sources as before I1, so the v3 path is byte-for-byte unchanged. I1a checks this by comparing the preprocessed `msstyles.c`/`draw.c`/`system.c` with the option off against the pre-I1 commit. |
| Runtime | `HKLM\SOFTWARE\Leeway\UxTheme` `EnableV4` (REG_DWORD; anything other than 1 is off), read once per process at uxtheme start | **0** | A v4 style fails with `ERROR_BAD_FORMAT` exactly as today, and step 2.3.4 is not even reached: the switch is tested before the file is opened. |

- HKLM only: a user cannot enable it.
- No HKCU or policy override in I1.
- The theme applier (Personalization side) must not offer a v4 style while the runtime gate is off; it reads the same value.

## 5. Trust boundary: store, ACLs, elevation (enforcement points)

Jim 2 writes the policy text. This section designs where it is enforced. The owner rule (decision 3) and R-POL-1..6 apply.

### 5.1 The store

- **Location:** `%SystemRoot%\Resources\Themes\` (the existing location of `win8.msstyles`), one subdirectory per style, with its `.mui` files under `<lang>\`.
- **ACL, set by setup (the `hivesys`/INF that creates the directory) and by the installer:**
  - owner `TrustedInstaller` (or SYSTEM);
  - SYSTEM and Administrators: full control;
  - Users and Authenticated Users: read and execute;
  - no other ACE;
  - inheritance limited to the store tree.
- **Installing** (copying into the store) is an administrator action (R-POL-1). **Choosing** an installed style is a user action that writes `ThemeManager` (HKCU), as today.

### 5.2 At load, in every process (uxtheme, step 2.3.2)

Checks, in order. A failure means the style is not loaded (fail closed, section 7):

| # | Check | How |
|---|---|---|
| L1 | the path is inside the store | `GetFullPathNameW`, then the opened handle's `GetFinalPathNameByHandleW` must begin with the store path (case-insensitive) |
| L2 | no reparse point on the file or its directories | `FILE_FLAG_OPEN_REPARSE_POINT` + `FILE_ATTRIBUTE_REPARSE_POINT` test on the file and on each directory up to the store root |
| L3 | local fixed volume | volume of the handle: `GetDriveTypeW` on the final path = `DRIVE_FIXED`; no UNC |
| L4 | not writable by unprivileged users | owner ∈ {TrustedInstaller, SYSTEM, Administrators}. No ACE granting write, append, delete, `WRITE_DAC` or `WRITE_OWNER` to Users, Authenticated Users, Everyone, INTERACTIVE, or the caller's user SID, on the file and on each directory up to the store root |
| L5 | crash guard clear | 5.4 |

**Which processes enforce L1–L4 (DECISION):**
- **Every process, for v4 styles** (R-POL-1).
- **Elevated or privileged processes, for v3 styles too** (owner rule: privileged processes never read theme resources from a user-writable place).
- Non-elevated processes keep the v3 behaviour in I1 (any path, as today). This is a compatibility choice for Jim 2 to confirm or tighten; I1 logs a v3 load outside the store in non-elevated processes so the effect can be measured.

**Elevation test** (once per process, on the **process** token; the result is cached). The process counts as privileged when any of these holds:
- `TokenElevation.TokenIsElevated` is TRUE;
- `TokenIntegrityLevel` is High or System;
- the token user is LocalSystem, LocalService or NetworkService.

LiberNT supports `TokenElevation`/`TokenElevationType` and `TokenIntegrityLevel` (SOURCE: `ntoskrnl/se/tokencls.c:163-167,1609`). If any query fails, the process counts as privileged (fail closed).

### 5.3 At install (Personalization/installer side, separate from DWM)

- The installer copies the candidate into a temporary directory and runs `tl4check.exe` on it. `tl4check.exe` is a small console program that links the same `sdk/lib/tl4` and calls the same validation and model build as uxtheme (R-GEN-10, R-POL-3).
- It runs in a throwaway process with a restricted, low-integrity token. Only on success does the elevated installer move the files into the store and set the ACL.
- A style that fails is never offered and never written to `ThemeManager`.
- `.theme` paths resolve only inside the store (R-POL-6). Companion binaries are never installed.

### 5.4 Crash guard (R-POL-5)

- Before step 2.3.6 the process writes a volatile mark, `HKCU\Software\Leeway\UxTheme\Loading\<hash of the style path>` = its PID, and deletes it after step 2.3.8.
- At start, a mark whose PID is no longer running and whose style is the active one means a previous load died. The style is skipped (classic) and the mark is kept until the user chooses a style again.
- Volatile key: the mark does not survive a reboot. DECISION for Jim 2: whether a reboot should clear it.

## 6. Metrics and system settings

- **I1a:** a v4 style changes **no** system setting. `MSSTYLES_SetActiveTheme(setMetrics=TRUE)` skips the `sysmetrics` class for v4, and `MSSTYLES_FindMetric` returns not found, so the system's current metrics stay.
- **I1b:** after the R-MOD-7 clamp table is in REQUIREMENTS.md, the adapter applies `sysmetrics` through that table only (R-MOD-3). A value out of range rejects that property; it is not repaired.

## 7. Rollback and fail-closed

- **Per load:** any error → no partial `THEME_FILE`, `OpenThemeFile` returns the error, and the process uses the classic look.
  - Same behaviour as today's `ERROR_BAD_FORMAT` (VERIFIED on cand3 for all 32 reference styles, research 01 §8.2).
  - The service and the applier must not write a style that failed (R-POL-3).
- **Per machine:** `EnableV4` = 0 → the next process start loads no v4 style. A user whose active style is v4 gets classic until they choose another style. Nothing else changes.
- **Per build:** `UXTHEME_V4` OFF → the pre-I1 binary behaviour (section 4).
- **Crash at load:** guarded by 5.4.

## 8. Tests

| Test | Where | Pass |
|---|---|---|
| Existing uxtheme apitests (`modules/rostests/apitests/uxtheme/*`: ThemeRendering, ThemeDpi, ThemeSysSize, NonClient*, MenuTheme, SetWindowTheme, …) and winetests uxtheme | CI + lab, with the build gate OFF and ON with the runtime gate 0 | no new failures against the pre-I1 baseline |
| v3 look unchanged | new apitest: render a fixed set of parts (BUTTON, SCROLLBAR, WINDOW caption, TAB, PROGRESS) from `win8.msstyles` into DIBs; compare a hash against the baseline taken before I1 | identical hashes in all three gate states |
| Byte-identical build | preprocessed sources with `UXTHEME_V4` OFF vs the pre-I1 commit | identical |
| v4 positive | new apitest with **generated** v4 styles (TL4 `tests/v4fixture.c` ported to write a file at test time; R-PROC-1: no reference asset): `OpenThemeDataFromFile`, then `GetThemeColor/Int/Bool/Enum/Margins/Rect/Position/IntList/String/Font` per known tuple; `DrawThemeBackground` of a generated PNG part | values equal the fixture's; image pixels equal |
| v4 negative | the same generator with each TL4 reject reason (one per R-V4/R-RES id) | load fails, process classic, no crash, guard mark cleared |
| Trust | styles outside the store, behind a junction, with a Users-writable ACE, on a share; tested from a normal and from an elevated test process; also v3 from an elevated process | refused where 5.2 says |
| Reference corpus | lab VM only: the 32 reference styles from `TL4_CORPUS_DIR`, never committed | 32/32 open; per-tuple values equal the TL4 corpus dump |
| Windows parity | controlled Windows VM (owner decision 2: no patchers, no DLL replacement, host unchanged); stock `aero.msstyles` only; `GetTheme*` per (class, part, state, property) | Windows build recorded; differences listed, not hidden |
| Performance | lab: load time and per-process memory at corpus maximum | recorded per commit (no target yet: HYPOTHESIS below 50 ms, below 2 MB) |

Pam mu3j's `03-RUNTIME-VERIFICATION.md` results feed the lab baseline when they arrive.

## 9. Milestones

| Milestone | Content | Entry | Exit |
|---|---|---|---|
| **I1a** load + controls | `sdk/lib/tl4` vendored; both gates; load sequence 2.3; trust checks 5.2 (v4 every process, v3 elevated); crash guard; class and property dispatch; getters for INT/BOOL/ENUM/COLOR/POSITION/RECT/MARGINS/INTLIST/STRING/FONT; images by id (2.5); no metrics | this design OK'd by Jim 2; MSVC build of the TL4 core; S1 report before the build gate is turned on in lab builds | section 8 tests green with the gates off and on; corpus 32/32 in the lab |
| **I1b** getters + metrics | clamp table (R-MOD-7) then `sysmetrics`/`FindMetric`; `GetThemeStream`/`GetThemeBitmap`; `GetThemeFilename`; documentation properties; `GetThemePropertyOrigin` semantics checked on Windows | I1a done; R-MOD-7 table merged | apitests per getter; Windows parity for the stock style |
| **I1c** install path | `tl4check.exe`; installer copy-validate-ACL; applier offers v4 only with the gate on | Jim 2's policy text | trust tests of section 8 from the installer side |
| later, separate | frame painter (`DWMWindow`), atlas images, animations, high-contrast types 240/241/243, dark-mode classes | own design | — |

Enabling `EnableV4` outside the lab requires: S2 closed, I1a–I1c done, and Jim 2's review of the final policy enforcement (owner decision 3).

## 10. Open points

1. `GetThemePropertyOrigin`: TL4's `origin` is where the record sits; v3 reports where the lookup found it. Measure on Windows in I1b.
2. `BCMAP` vs uxtheme's name-built `overrides` for `App::Class`: TL4 follows BCMAP (L2 design note). Parity with Windows is checked in the corpus test.
3. Which `ThemeManager` `ColorName`/`SizeName` the applier writes for v4: VMAP gives `NormalColor`/`NormalSize`, while the v3 `win8.msstyles` uses `Normal Color`/`Normal Size` (research 05 §2.2.8).
4. Sharing one model between processes (a read-only section built by the theme service) would save memory. It is not in I1; it would need its own trust review.
5. R-PROC-3 reading (section 0): Jim 2.
