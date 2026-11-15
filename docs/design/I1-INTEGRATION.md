# I1: integration of the version-4 loader into LiberNT uxtheme (design)

Author: Stanley (Leeway Open Software), 2026-11-15.

Status: **design v2**, after Jim 2's review of v1:
- architecture accepted;
- 13 MUSTs on trust and gates, all answered here (section 11).

No implementation starts before his PASS.

**Authority:** owner decisions of 2026-11-15:
- (1) R2/I1 authorized: v3 stays operational; v4 is added incrementally behind a controlled boundary, with regression tests and rollback; no wholesale rewrite of uxtheme, Explorer or DWM.
- (2) Windows reference testing authorized in a controlled Windows environment.
- (3) Privileged processes never load theme resources from user-writable locations.
- (4) No GitHub spending increase; Lab01 is evaluated as a local build/test machine.
- (T-SIG-4) An administrator may approve unsigned reference styles into `Machine`.

**Normative documents** (they win over this design):
- `docs/security/REQUIREMENTS.md`, R-PROC-3 as amended in a5c687e;
- `docs/security/THEME-TRUST-POLICY.md` v2 (a5c687e).

T-* and R-* ids are cited from them.

**Inputs:**
- `docs/design/V4-LOADER-SKETCH.md` (05) §2.2;
- `docs/design/L2-MODEL-DESIGN.md`;
- LiberNT research `research-dwm/02`, `04` and `REPORT-DWM-VISUAL-STYLES.md`.

**Labels:**
- **VERIFIED:** observed, method stated.
- **SOURCE:** path:line. TL4 at main a5c687e; LiberNT at public `leeway/main` ad21954.
- **HYPOTHESIS:** expected, to be measured.
- **DECISION:** a choice of this design, open to review.

## 0. Gates and non-goals

- **Non-goals:**
  - no rewrite of uxtheme, Explorer or DWM;
  - no frame painter (separate milestone);
  - no DirectUI integration;
  - no change to the v3 parser beyond the hooks of section 2 and the trust gate of section 5.
- **R-PROC-3 (as amended):**
  1. After this review closes, I1 code may be merged with the v4 hooks compiled out.
  2. The `UXTHEME_V4` build option is turned on only in lab builds, and only after the S1 report **and** the R-MOD-7 table (section 6).
  3. The v4 path is switched on outside the lab only after S2, the R-MOD-7 table in REQUIREMENTS.md, and T-GATE-1.
- **The trust gate of section 5 is not behind any of these.** It ships with I1a in default builds and applies to the v3 loader in every process (T-GATE-2, T-GATE-4).

## 1. Where uxtheme stands today (SOURCE)

- **Open:** `MSSTYLES_OpenThemeFile` (`dll/win32/uxtheme/msstyles.c:99`):
  - maps the style with `LoadLibraryExW(path, LOAD_LIBRARY_AS_DATAFILE)` (`:115`);
  - requires `PACKTHEM_VERSION` = 3 (`:122-134`);
  - parses INI text resources.
- **Model:** `THEME_FILE` → `THEME_CLASS` → `THEME_PARTSTATE` → `THEME_PROPERTY` lists (`msstyles.h`).
  - Values are INI text (`lpValue`), re-parsed by `MSSTYLES_GetProperty*` (`msstyles.c:1723-1915`).
  - Lookups: `MSSTYLES_FindProperty` (`:1421`), `FindPart`/`FindPartState` (`:532,564`), `FindMetric` (`:745`), `FindAppClass` (`:1206`).
- **Outside `msstyles.c`:**
  - `draw.c:268,326` copies `lpValue` as an image name;
  - `ncscrollbar.c:131` and `system.c:1120` ask for part-states;
  - `metric.c:49-238` reads metrics;
  - `system.c:1171` reads INI documentation values.
- **Settings:** the style is selected by `HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager` `DllName` (a full path), `ColorName` and `SizeName` (`system.c:149-200`), with no location or owner check.
  - The default is `%SystemRoot%\Resources\Themes\Aero\aero.msstyles` (`boot/bootdata/hivedef.inf:1952`).

## 2. The boundary (accepted in review; unchanged except 2.3 ordering)

### 2.1 Shape

**Delegate, do not convert.**
- A v4 `THEME_FILE` holds the TL4 model, and a v4 `THEME_CLASS` holds a class ordinal.
- Lookups call `tl4_model_find` (TL4 `loader/include/tl4/loader/model.h`), which mirrors `MSSTYLES_FindProperty`'s order on the BCMAP chain (R-MOD-6).

The adapter is one new file, `dll/win32/uxtheme/msstyles_v4.c`. The edits to existing files are dispatch points (2.4): `if (tf->v4) return MSSTYLES_V4_...;` placed before the v3 code, all under `#ifdef UXTHEME_V4`.

### 2.2 Data structures (added fields, v3 values unchanged)

| Struct | Added | Meaning |
|---|---|---|
| `THEME_FILE` | `struct _V4_FILE *v4` | NULL for v3. For v4: file and section handles, read-only views (style, `.mui`), the `tl4_model` and its buffer, the pixel budget, the shadow array |
| `THEME_CLASS` | `UINT v4Ordinal` | TL4 class ordinal |
| `THEME_PROPERTY` | `const tl4_prop *v4` | NULL for v3. For v4, `lpValue` = NULL; type, id and origin come from the `tl4_prop` |
| `THEME_IMAGE` | `UINT v4Id` | IMAGE/STREAM id for v4 |

**Shadow array:**
- One `THEME_PROPERTY` per `tl4_prop` (at most 32,768), filled at load.
- `MSSTYLES_FindProperty` returns `&shadow[p - model.props]`: stable, read-only after load.

Size: HYPOTHESIS, about 1 MB per process at corpus maxima; measured in I1a.

### 2.3 Load sequence

Steps 1–4 are the trust gate (section 5). It runs for v3 and v4 alike, in every process, and is built in every build.

1. **Select (5.3).** Three names from the settings; the loader builds the path. Nothing usable means the built-in look.
2. **Open once (5.4 step 1)** and check everything on the handle (5.4 steps 2–6).
3. **Crash guard (5.6):** read the marks, then write the mark. A failed write means no load.
4. **Version peek** with the bounded reader on a read-only view of the handle: `tl4_pe_open` + `tl4_pe_find(PACKTHEM_VERSION)`.
   - **3:** unmap. Under T-LOAD-2's exception, the v3 loader may reopen the same path with its existing `LoadLibraryExW` (`msstyles.c:115`). This happens only because steps 1–2 passed on the handle. The checked handle stays open, with write and delete denied, until the v3 load returns, so the file cannot be replaced between check and reopen. If the T-GATE-4 rollback is active for this ordinary process, the exception does not exist and v3 behaves as before I1 (5.8).
   - **4:** without `UXTHEME_V4`, or with the runtime gate for this process class off (section 4): `ERROR_BAD_FORMAT`, as today. Otherwise continue at step 5.
   - **Anything else:** `ERROR_BAD_FORMAT`.
5. **Strings (v4).** The `.mui` path is built by the loader from the fixed layout (T-STORE-3, T-LOAD-6): `<part>\<name>\<language>\<name>.msstyles.mui`. `<language>` comes from the user UI language, under the T-STORE-4 language rule, then `en-US`. It is opened and checked like the style (steps 2 and 5.4). No MUI redirection.
6. **Validate and build (v4):** `tl4_v4_validate` → `tl4_model_size` → aligned buffer → `tl4_model_build` (R-MOD-5, R-GEN-10, T-LOAD-5); unresolved references within R-V4-12.
7. **Variants (v4):** colour and size names from `VMAP`. An unlisted name is `E_PROP_ID_UNSUPPORTED`.
8. **Shadow array (v4); clear the guard mark (v3 and v4); return.**

Any failure: free everything, no partial state, clear the mark only for a clean refusal (T-GUARD-1), then fail closed (section 7).

### 2.4 Dispatch points

| Existing function (SOURCE) | v4 behaviour |
|---|---|
| `MSSTYLES_FindAppClass` `msstyles.c:1206` | `App::Class`, then `Class`, through `tl4_model_class`; over 120 code units means not found |
| `MSSTYLES_FindProperty` `:1421` | `tl4_model_find` → shadow entry; the primitive type must equal `tl4_prop.type` |
| `MSSTYLES_FindPart` / `FindPartState` `:532,564` | `tl4_model_part_defined`. SHOULD from review: before any sentinel is returned, I1a lists **every** caller of `FindPartState` (known: `system.c:1120`, `ncscrollbar.c:131`, `msstyles.c` internals) and states what each one reads from the result. A sentinel is used only if no caller dereferences it beyond NULL-testing; otherwise the caller gets a v4 branch |
| `MSSTYLES_FindMetric` `:745`, `metric.c` | I1a: not found for v4. I1b: the `sysmetrics` class through the range table (section 6) |
| `MSSTYLES_GetProperty*` `:1723-1915` | `if (tp->v4)` → `tl4_get_*` → range table (section 6) → result. Any TL4 error or out-of-range value → `E_PROP_ID_UNSUPPORTED` |
| `draw.c:268,326` | `tp->v4` → `MSSTYLES_V4_LoadImage` |
| `GetThemeDocumentationProperty` (`system.c:1171`) | `tl4_model_find_rmap` |
| `OpenThemeFile` / `OpenThemeDataFromFile` with a caller path | section 5.9 (T-LOAD-7) |

### 2.5 Images (R-RES-3..7), accepted

1. `tl4_get_filename` → (id, offset, size).
2. Size check, then a private copy of exactly `size` bytes.
3. `tl4_png_check` on the copy, then `tl4_pixel_budget_take`.
4. Decode the copy from memory (`pngsup.cpp`, GDI+) and compare the dimensions.
5. Create the HBITMAP and cache it by `v4Id`.

Any failure: not found.

## 3. Build model (accepted)

- **Vendoring.** TL4 `loader/include` and `loader/src` are vendored as `sdk/lib/tl4/` and linked statically into `uxtheme.dll` and `tl4check.exe` (5.10).
- **Pin.** `sdk/lib/tl4/TL4_COMMIT` names the TL4 main commit. SHOULD from review: `sdk/lib/tl4/MANIFEST.sha256` lists the SHA-256 of every vendored file. A CI step recomputes it, so a local edit of the vendored copy fails the build.
- **Host portability.** TL4 stays free of Windows code; all OS code is in the adapter and in `trust.c`.
- **MSVC.** An MSVC build of the TL4 core (R-GEN-9) is an I1a entry criterion, run on Lab01 or the private runner (owner decision 4).

## 4. Switches

| Switch | Where | Default | Scope | Effect when off |
|---|---|---|---|---|
| Build `UXTHEME_V4` | CMake option in `dll/win32/uxtheme/CMakeLists.txt`; links `sdk/lib/tl4` | **OFF** | v4 hooks only | Every v4 hook is `#ifdef`-ed out. "Byte-identical" covers **only these hooks**: with the option off, preprocessing `msstyles.c`, `draw.c` and `system.c` gives the same output as the I1 commit with the hooks removed. The trust gate (`trust.c`) is **not** under this option. |
| Runtime, ordinary processes (T-GATE-3) | `HKLM\SOFTWARE\Leeway\UxTheme` `EnableV4Ordinary` (REG_DWORD, exactly 1 = on) | **0** | v4 loads in processes that 5.2 classifies ordinary | v4 style → `ERROR_BAD_FORMAT`, before any v4 parsing |
| Runtime, privileged processes (T-GATE-3) | same key, `EnableV4Privileged` | **0**, and stays 0 until T-GATE-1 is met | v4 loads in privileged processes | same |
| v3 rollback for ordinary processes (T-GATE-4) | same key, `V3AnyPathOrdinary` (exactly 1 = on) | **0** (checks on) | ordinary processes only; **never** read by a privileged process | 5.8 |

- **The key.** `HKLM\SOFTWARE\Leeway\UxTheme` is created by setup with protected DACL `O:BAG:SYD:PAI(A;CI;KA;;;SY)(A;CI;KA;;;BA)(A;CI;KR;;;BU)`. A value that cannot be read, has the wrong type, or sits in a key whose DACL fails the T-DEF-1-style check counts as **off**. The key's DACL is checked with the same pure function as files (5.5), on the key's security descriptor.
- **Test (MUST 11).** A process running as an ordinary user fails to set any of the four values (`ERROR_ACCESS_DENIED`). The image test also reads the key's DACL back.
- **Reading.** Each value is read once per process, before the first style open.
- **Applier.** The theme applier offers a v4 style only when the gate for the current process class is on. It reads the same values.

## 5. Trust gate: enforcement of THEME-TRUST-POLICY.md v2

### 5.1 System facts (T-GATE-1, T-STORE-6, policy §12)

| Fact | Evidence |
|---|---|
| The system volume is **NTFS**; the boot/ESP volume is FAT | VERIFIED 2026-11-15, read-only inspection of a CI-built `LiberNT.img` (clang, libernt-srv lab copy): partition 1 type EF, boot sector `mkfs.fat`; partition 2 type 07, boot-sector OEM id `NTFS`. SOURCE: `boot/boot_images.cmake:690-729` (64-MiB FAT boot partition + NTFS system partition built with the host tool `ntfsimg`) |
| The image builder writes file descriptors | SOURCE: `boot/boot_images.cmake:924-929` (`ntfsimg --apply-security … boot/bootdata/filesecurity.txt`, also `:980,1104`) |
| Descriptors on the `Windows` tree, `Resources\Themes` included | SOURCE: `boot/bootdata/filesecurity.txt:3-4,15-17`. Directories `{COMPONENTDIR}`: owner TrustedInstaller SID, protected DACL: TI full; SYSTEM and Administrators `0x1301bf` (modify); Users, ALL APPLICATION PACKAGES and restricted app packages read `0x1200a9`; inherit-only GA for CREATOR OWNER/TI/SY/BA. Files `{COMPONENTFILE}`: owner TI, TI full, SY/BA/BU/AC/RAP read only. No later line overrides `Windows/Resources` (SOURCE: no match for `Resources` or `Themes` in the file) |
| The shipped NTFS driver is `ntfs_new` → `ntfs.sys` | SOURCE: `drivers/filesystems/CMakeLists.txt:12`; `drivers/filesystems/ntfs_new/CMakeLists.txt:22-34`; `boot/bootdata/hivesys.inf:2023-2028` |
| It enforces stored descriptors at open | SOURCE: `ntfs_new/create.c:417-612` (`NtfsCheckFileAccess`, traverse, `FILE_DELETE_CHILD` on the parent) → `ntfs_new/security.c:97-139` (`SeAccessCheck`); parent check at create `create.c:912`; a record without a descriptor gets `O:BAG:SYD:(A;;FA;;;SY)(A;;FA;;;BA)` (`security.c:16-94`) |
| Registry keys are access-checked | SOURCE: `ntoskrnl/config/cmparse.c:252` (parent, create), `:915` (`ObCheckObjectAccess`, open); `cmsysini.c:1130` (`CmpSecurityMethod`) |
| Token classes for 5.2 | SOURCE: `ntoskrnl/se/tokencls.c:163-167,1609` (`TokenElevation`, `TokenElevationType`; `TokenIntegrityLevel` in the same file). Whether logon assigns split tokens and integrity levels as on Windows: **PENDING runtime check** (5.11) |
| The descriptors are actually present on a booted system | **PENDING**: lab `cacls` on `C:\Windows`, `\Resources`, `\Resources\Themes`, `\Themes\Aero`, `aero.msstyles` (requested 2026-11-15). Indirect: `System32\leewaydisp.cpl` could not be renamed in the lab ("Access denied", Pam mu3j, lab-a7c253931ac row 10) |

**The TrustedInstaller question** (review MUST 4: not trusted unless shown to exist):
- The image makes the TI service SID (`S-1-5-80-956008885-…`) the **owner** of the whole `Windows` tree (`filesecurity.txt:1-4`).
- No TrustedInstaller **service** is defined: `hivesys.inf:69,209` only list the name under `SafeBoot`, and there is no `Services\TrustedInstaller` key.
- The service control manager has no per-service SID support (SOURCE: no `SERVICE_SID_TYPE` handling in `base/system/services`).
- So no token on the system can carry the TI SID today (HYPOTHESIS until the runtime check of 5.11 lists the SIDs of running service tokens).
- Under T-DEF-1, an owner outside {SYSTEM, Administrators} makes the object user-writable. **Therefore the shipped `Resources\Themes` tree fails T-DEF-2 as written**, and privileged processes would get no v3 style.

The same holds for the **parents**: T-DEF-2 requires a trusted owner on every directory up to the volume root, and `dir /` and `dir Windows` are TI-owned too (`filesecurity.txt:12,15`).

DECISION, two parts:
- (a) This design does **not** add TI to the trusted principals.
- (b) Setup re-owns to `Administrators`:
  - the store trees, with DACLs set to T-STORE-5: `Styles4` (new) and the v3 location `Resources\Themes` (5.7);
  - the **directory objects only** (not their contents) of the volume root, `Windows` and `Windows\Resources`, keeping their DACLs.

  This is done by new `filesecurity.txt` lines, applied by `ntfsimg --apply-security` and checked by the image test (T-STORE-7).

The alternative is for Jim 2 to rule the TI SID trusted on this evidence (no token can carry it). That avoids changing the owner of `C:\` and `C:\Windows`. The design works either way; **this is the one decision in v2 that needs his ruling before I1a-T** (section 10 item 3).

### 5.2 Privileged classification (T-DEF-3, T-DEF-4, T-PRIV-*)

`trust.c: UXTHEME_ClassifyProcess()` runs once per process, before the first theme open, on the **primary** token (`OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY)`), never a thread token. The result is cached. The process is privileged when **any** of these holds:

| Clause | Query |
|---|---|
| user is SYSTEM, LocalService or NetworkService | `TokenUser` = S-1-5-18/19/20 |
| Administrators present and enabled (not deny-only) | `TokenGroups`: S-1-5-32-544 with `SE_GROUP_ENABLED` and without `SE_GROUP_USE_FOR_DENY_ONLY` |
| elevated | `TokenElevationType` = `TokenElevationTypeFull`, or `TokenElevation.TokenIsElevated` |
| integrity high or above | `TokenIntegrityLevel` RID ≥ `SECURITY_MANDATORY_HIGH_RID` |
| session 0 or started by the SCM | `TokenSessionId` = 0; or the parent is `services.exe` (parent PID from `NtQueryInformationProcess(ProcessBasicInformation)`, image checked through the parent handle, and the parent's start time earlier than ours, so a recycled PID cannot match) |
| any listed privilege, enabled or not | `TokenPrivileges` contains one of the ten T-DEF-3 privileges |
| logon or secure desktop | `GetThreadDesktop(GetCurrentThreadId())` → `GetUserObjectInformationW(UOI_NAME)` is `Winlogon` or `Screen-saver`, or the window station is not `WinSta0` |

- **Failure.** Any query that fails means privileged (T-DEF-4, T-FAIL-3).
- **Unit tests.** The classifier is a pure function over a struct of these facts, unit-tested on the host (SHOULD from review); `trust.c` only gathers the facts.
- **Rules applied to a privileged process:**
  - **T-PRIV-1:** looks only in `Machine` (5.7).
  - **T-PRIV-2:**
    - reads only the three names, with lengths checked and the name rule applied (5.3);
    - a process whose token user is SYSTEM/LS/NS, or is in session 0, reads **no** user setting and uses the machine default (`HKLM\SOFTWARE\Leeway\UxTheme` `MachineDefault`, a style name under T-STORE-4, protected like the gate key).
  - **T-PRIV-3:** opens no file named by a `.theme` or a per-user theme setting unless its location passes T-DEF-2. In I1 uxtheme opens no such file, so the rule lands on the applier and on wallpaper/cursor/sound loading (I1c, 5.10).
  - **T-PRIV-4:** when the calling thread impersonates, uxtheme does not load or cache a style for that call. It keeps the process's current look.
  - **T-PRIV-5:** a theme-change notification only re-runs 5.3–5.6. It carries nothing that is used. At most one reload in 5 s; later notifications coalesce into one deferred reload.
  - **T-PRIV-6:** on the logon or secure desktop, the machine default only.
  - **T-PRIV-7:** a refusal shows nothing. It writes one event (`ReportEventW`, fixed message id, the style name after the name rule; no path), at most once per style per process.

### 5.3 Selection by name (T-LOAD-1, T-STORE-4, T-PRIV-2)

- **Inputs.** From `ThemeManager`, three strings, each read with a size bound (`RegQueryValueExW` into a fixed buffer, type `REG_SZ`/`REG_EXPAND_SZ` taken as **text**, never expanded):
  - `DllName`: today a full path;
  - `ColorName`, `SizeName`: names.
- **Style name from `DllName`.** The text must be **exactly** one of the paths the loader builds (5.7) for some name that matches T-STORE-4. The comparison is case-insensitive and on the text as stored, with no expansion, no normalisation, no `\\?\` and no short names.
  - The loader builds the candidates from `GetSystemWindowsDirectoryW` (T-STORE-1; never `%SystemRoot%` text). Because today's stored values are `%SystemRoot%\…` literals (`hivedef.inf:1952`), the literal string `%SystemRoot%` followed by the built relative path is also accepted as an exact match. It is treated as a fixed token, not expanded.
  - DECISION: I1c moves the applier to write a new value `StyleName` (a name only). When it is present it is used, and `DllName` is ignored.
- **Colour and size names:** T-STORE-4's name rule, else the style's default variant.
- **Anything else** selects nothing (T-FAIL-1).

### 5.4 Open once, check on the handle (T-LOAD-2, T-LOAD-3, T-DEF-1, T-DEF-2)

1. **Open.**
   - Open the built path with `CreateFileW(GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT)`; write and delete are denied to others.
   - Open each directory from the file's parent up to the volume root the same way, with `FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT` and `FILE_READ_ATTRIBUTES | READ_CONTROL`, `FILE_SHARE_READ`. These handles are held until the load ends, so no component can be renamed or replaced (rename and delete need the parent's `FILE_DELETE_CHILD`, which these handles do not block, but the final-path check below runs after all opens).
   - All checks below use these handles only.
2. **Type.** `GetFileInformationByHandleEx(FileAttributeTagInfo)`: the file is not a directory and not a reparse point; each directory has no `FILE_ATTRIBUTE_REPARSE_POINT`. `GetFileType` = `FILE_TYPE_DISK`.
3. **Final path equals the built path.** `GetFinalPathNameByHandleW(h, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS)` on the file is compared for **equality** (case-insensitive, full string) with the built path in the same form. No prefix comparison: this refuses `Themes2`, 8.3 names, `\\?\` forms and alternate streams. The directory handles' final paths must equal the built path's ancestors.
4. **Volume, from the handle.**
   - `GetVolumeInformationByHandleW`: `FILE_PERSISTENT_ACLS` must be set (T-STORE-6) and the file system name must be `NTFS`.
   - The volume GUID path of the handle (`GetFinalPathNameByHandleW(VOLUME_NAME_GUID)`) must equal the volume of the Windows directory (`GetVolumePathNameW` of `GetSystemWindowsDirectoryW` → `GetVolumeNameForVolumeMountPointW`). That proves a local fixed system volume without trusting a drive letter.
   - "Unknown" fails (T-FAIL-3).
5. **Not user-writable (5.5).** `GetSecurityInfo(h, SE_FILE_OBJECT, OWNER | DACL)` on the file and on every directory handle up to the **store root** (T-DEF-1). For directories above the store root up to the volume root, the narrower T-DEF-2 test applies (owner trusted; no `DELETE`, `WRITE_DAC`, `WRITE_OWNER` or `FILE_DELETE_CHILD` for an ordinary principal).
6. **Size and map** from the same file handle (T-LOAD-4): `GetFileSizeEx` ≤ 32 MB, then `CreateFileMappingW(h, PAGE_READONLY)` of exactly that size, then a read-only view. Never `SEC_IMAGE`, never `LoadLibrary` for v4.

The `.mui` (and later `shellstyle.dll`) go through 1–6 with their own built paths (T-LOAD-6).

### 5.5 The writable test (T-DEF-1, inverted as required by review MUST 7)

`trust.c: UXTHEME_SdIsUserWritable(PSECURITY_DESCRIPTOR sd, BOOL isDirectory, BOOL aboveStoreRoot)` is a **pure function** over a self-relative security descriptor, unit-tested on the host with hand-built descriptors (SHOULD from review). It returns *writable* when **any** of these holds:
- the descriptor cannot be read or parsed (the caller passes NULL);
- the owner is not S-1-5-18 or S-1-5-32-544;
- there is no DACL, or a NULL DACL;
- an **allow** ACE that is effective on this object (not `INHERIT_ONLY_ACE`) gives a SID other than S-1-5-18 or S-1-5-32-544 any write-class right.

Write-class rights:
- for a file: `FILE_WRITE_DATA`, `FILE_APPEND_DATA`, `FILE_WRITE_EA`, `FILE_WRITE_ATTRIBUTES`, `DELETE`, `WRITE_DAC`, `WRITE_OWNER`, `GENERIC_WRITE`, `GENERIC_ALL`, `MAXIMUM_ALLOWED`;
- for a directory, additionally `FILE_ADD_FILE`, `FILE_ADD_SUBDIRECTORY`, `FILE_DELETE_CHILD`.

Generic bits are mapped with `FILE_GENERIC_*` before the test, and also count as write-class on their own.

Further rules:
- `aboveStoreRoot` narrows the right set to `DELETE`, `WRITE_DAC`, `WRITE_OWNER`, `FILE_DELETE_CHILD` (T-DEF-2).
- Deny ACEs are ignored; they never make an object safe.
- Callback, object and conditional ACE types make the result *writable*: they are unknown, so they count as a refusal.
- SIDs are compared exactly. `CREATOR OWNER`, `Authenticated Users`, `Users`, `Everyone`, app-package SIDs and any service SID count as ordinary.

### 5.6 Crash guard (T-GUARD-1..6), in front of both loaders (review MUST 8)

- **Mark.** `HKCU\Software\Leeway\UxTheme\Guard` holds one value per (part, style): name `<part>:<name>`, data {PID, process creation time from `GetProcessTimes`}.
  - The key is non-volatile, so it survives a reboot (T-GUARD-1).
  - For SYSTEM/service processes, the token user's own hive is the machine default's guard under `HKLM\SOFTWARE\Leeway\UxTheme\Guard` (T-GUARD-3, T-GUARD-5).
- **Order.** The guard is read, then written, at 2.3 step 3: after the checks on the handle, before the version peek, and so before the v3 parser too.
- **Stale mark.** For every value of the same (part, style), the process is looked up with `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` and its creation time compared. A mark whose process is gone, or whose PID now belongs to a process with another start time, is stale: the style is skipped (T-GUARD-2) and the mark kept.
- **Clearing.** The skip ends when the applier applies a style on purpose (it deletes the marks of that style), or when the installer reinstalls or removes the style.
- **Write failure.** If the mark cannot be written, the style is not loaded (T-GUARD-4).
- **Clean finish.** After a successful load or a clean refusal, the process deletes its own value.
- **T-GUARD-6:** the applier (I1c) performs the first load of a newly chosen style in its own process before writing the choice.

### 5.7 The store (T-STORE-1..8, T-PRIV-1)

| Item | Location | Notes |
|---|---|---|
| store root (v4) | `<GetSystemWindowsDirectoryW>\Resources\Styles4` | |
| parts | `…\Styles4\Machine`, `…\Styles4\Local` | a style is in exactly one part (T-STORE-2) |
| layout | `<part>\<name>\<name>.msstyles`, `<part>\<name>\<language>\<name>.msstyles.mui`, `<part>\<name>\Shell\<colour>\shellstyle.dll` | fixed (T-STORE-3) |
| v3 location (T-STORE-8) | `<windir>\Resources\Themes\<name>\<name>.msstyles` | the only v3 path; counts as `Machine` only while 5.4–5.5 pass |
| ACL (T-STORE-5) | `O:BAG:SYD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;AU)` on `Styles4`, both parts and `Resources\Themes` | set by new `filesecurity.txt` lines through `ntfsimg --apply-security` (5.1 decision b); read back by the image test (T-STORE-7) |

- **Part choice.** A privileged process looks only in `Machine`, and in `Resources\Themes` for v3 (T-PRIV-1). An ordinary process looks in `Machine`, then `Local`. A name present in both is an installer error; the loader takes `Machine` and logs it.
- **v3 has no `Local`.** Every v3 style counts as `Machine` while it passes the checks.

### 5.8 v3 in ordinary processes, and its only rollback (T-GATE-4, review MUST 3)

- 5.3–5.6 apply to the v3 loader in **every** process from I1a on.
- The only exception is `V3AnyPathOrdinary` = 1 (section 4). With it, an **ordinary** process loads v3 styles as before I1 (any `DllName` path, `LoadLibraryExW`, no location check), and logs each such load (one event per style per process).
- A privileged process never reads that value.
- The value is removed (code and setup) in the release that ships the installer (I1c).

### 5.9 Caller-given file names (T-LOAD-7)

`OpenThemeFile`, `OpenThemeDataFromFile`, `GetThemeDefaults`, `CheckThemeSignature` (`system.c`):
- **Privileged process:** the name must be **exactly** a built store path for some style name (5.3 rule), else `E_ACCESSDENIED` and the T-PRIV-7 event.
- **Ordinary process:** a store path goes through 5.4–5.6. Any other path is still opened under 5.4 steps 1, 2 and 6 (single open, data mapping, full validation), and logged (T-LOAD-7 SHOULD, kept for compatibility).

### 5.10 Install path (T-INST-1..9, T-SIG-1..6), I1c (review MUST 10)

1. **Staging.** `…\Styles4\.staging\` is created with T-STORE-5's ACL. The installer runs elevated and empties it at start (T-INST-7). Each candidate file is size-checked at the source (T-INST-3), copied once into staging and never read from the source again (T-INST-1). Only `.msstyles`, `.msstyles.mui`, and later `shellstyle.dll` are taken; everything else is reported and ignored (T-INST-2).
2. **Hash.** SHA-256 of each staged file (T-INST-6).
3. **Checker.** `tl4check.exe` (links `sdk/lib/tl4`, R-GEN-10) is started with:
   - a restricted token: Administrators deny-only, no privileges except `SeChangeNotifyPrivilege`;
   - the lowest integrity level available;
   - in a job: memory limit, CPU time limit, active-process limit 1, UI restrictions;
   - no network: the token has no network logon SID and the job has no network access, where available.

   It receives **inherited read-only handles** to the staged files, not paths. It runs the whole chain: PE reader, v4 validator, model build, every IMAGE and STREAM through `tl4_png_check` and a decode under the pixel budget, and every string table (T-INST-5). It reports only through its exit code. A crash, a timeout or a non-zero exit is a reject.
4. **Re-hash and place.** Hash again; it must equal step 2. Then move into `<part>\<name>\` on the same volume, under installer-chosen names (T-STORE-3/4). An existing name is never overwritten (T-INST-6).
5. **Part and origin (T-INST-8, T-SIG-2..5):**
   - default `Local`, mark *unsigned*;
   - `Machine` for a *system* style (hash in the shipped list) or an **approved** style. The owner decision of 2026-11-15 allows an administrator to approve an unsigned reference style into `Machine`: an explicit action that shows the file name, its SHA-256 and the statement that elevated programs will parse it (T-SIG-6). It is recorded in the installer log and in an origin file next to the style (`<name>.origin`: mark, hash, date, approving account).
   - *signed* is not used until a reviewed verifier exists (T-SIG-5).
6. **Removal and rejection.** Removal is an administrator action (T-INST-9). A rejected style is deleted from staging and never offered (T-INST-7).

Which LiberNT facilities exist for step 3 (restricted tokens, integrity levels, job limits, network isolation) is stated with evidence in I1c. A missing facility keeps the strict reading, and the gap is listed (policy §12).

### 5.11 Runtime checks still needed (T-GATE-1)

Run on a lab image, results into this section before the build gate is turned on:
- `cacls` on the paths of 5.1;
- a probe that prints, for an ordinary user, an administrator from Explorer, an elevated process and a service:
  - `TokenElevationType`, `TokenElevation` and `TokenIntegrityLevel` (confirms `tokencls.c:163-167,1609` on a running system);
  - the groups, with their enabled and deny-only flags;
  - the session id and the desktop name;
  - the SIDs of the running service tokens (the TI question).

## 6. Value ranges (R-MOD-3, R-MOD-7; review MUST 9)

- **Where.** Every value the adapter returns to uxtheme goes through one range table, applied in `msstyles_v4.c` after `tl4_get_*` and before the value leaves the adapter: every getter, not only metrics, because `draw.c` divides by and indexes with some of them.
  - Examples: image count (`TMT_IMAGECOUNT`) and frame counts are divisors; sizing margins, border sizes, font heights and INTLIST counts are bounds.
- **Out of range.** A value out of its range is a reject of that property (not found), never clamped (R-MOD-7).
- **Entry condition.** The table is an entry condition for turning the `UXTHEME_V4` build option on anywhere (section 0). Jim 2 moves it into REQUIREMENTS.md after S1 phase A. The adapter implements it from that text, and its host tests are driven by the same table.
- **I1a metrics.** I1a applies no `sysmetrics` value to the system. I1b applies them through the table only.

## 7. Fail closed and rollback (T-FAIL-1..4)

- **Per load.** Any refusal or validation failure:
  - the built-in look, or the machine default when that passes the same rules (T-FAIL-1);
  - no partial style, no second user choice, no retry in the same process run;
  - nothing outside the process changes (T-FAIL-2), except the guard mark (5.6), which the policy requires.
- **Across loaders (T-FAIL-4).** A style refused by the trust gate reaches neither loader. A v4 file never falls back to the v3 path or the other way round: the version peek is the only router.
- **Rollback, v4.** `EnableV4Ordinary` / `EnableV4Privileged` = 0, or a build without `UXTHEME_V4` (section 4).
- **Rollback, v3 trust gate.** `V3AnyPathOrdinary` = 1, for ordinary processes only (5.8). There is no rollback of the trust gate for privileged processes.

## 8. Tests

No shipped binary has a test bypass (T-GATE-5). Every style a test uses enters the store the way an administrator would: through the installer once it exists, and before that through a test helper that runs elevated and writes into the store with the store's ACL. Or the test uses a test build that is never shipped.

| Test | Pass |
|---|---|
| Existing uxtheme apitests (`modules/rostests/apitests/uxtheme/*`) and winetests, in every switch state of section 4 | no new failures against the pre-I1 baseline |
| v3 look unchanged: render fixed parts (BUTTON, SCROLLBAR, WINDOW caption, TAB, PROGRESS) from the shipped v3 style **installed in the store**, and hash the DIBs | equal to the pre-I1 baseline, in every switch state |
| v4 hooks byte-identical with `UXTHEME_V4` OFF (section 4) | identical preprocessed output |
| v4 positive: **generated** v4 styles (TL4 fixture generator writing a file at test time, R-PROC-1) installed into `Machine` and `Local`; `GetTheme*` per tuple; `DrawThemeBackground` of a generated PNG part | values and pixels equal the fixture's |
| v4 negative: one generated style per TL4 reject reason | refused, built-in look, guard mark cleared, no crash |
| T-LOAD-7: `OpenThemeDataFromFile` with a path outside the store, from a privileged test process | refused, event written |
| Gate key: an ordinary user sets each value of section 4 | `ERROR_ACCESS_DENIED` (MUST 11) |
| Range table: one out-of-range value per entry in a generated style | that property not found |
| **Policy §11, every bullet, by rule id**, for the v3 **and** the v4 loader: classification (one case per T-DEF-3 clause, failing query → privileged); location (Users write ACE, user owner, NULL DACL, parent `FILE_ADD_FILE` for Users, junction and symlink at each level, share, removable, FAT volume); selection (full path, relative, `..`, device name, environment reference, over-long, trailing dot or space, separator); swap after open; privileged (style only in `Local`, `.theme` naming a user file, impersonation, notification burst); install (executable in package, source changed after copy, validator crash, hang and memory limit, existing name, unsigned into `Machine` with approval off); fail closed (settings and store byte-identical after each refusal; refused by one loader not loaded by the other); crash guard (killed during load leaves a mark, next process skips, applying again clears, unwritable mark means no load); positive (system style in `Machine` loads in ordinary and privileged, approved style loads in both, unsigned in `Local` in ordinary only) | as the policy says |
| Host unit tests: `UXTHEME_SdIsUserWritable` and the classifier over hand-built inputs | every T-DEF case |
| Reference corpus (lab VM only, `TL4_CORPUS_DIR`, never committed): the 32 styles approved into `Machine` in the VM | 32/32 load; values equal the TL4 corpus dump |
| Windows parity: controlled Windows VM, stock `aero.msstyles`, no patchers (owner decision 2) | Windows build recorded; differences listed |
| Performance (lab) | load time and memory per process, recorded per commit |

## 9. Milestones

| Milestone | Content | Entry | Exit |
|---|---|---|---|
| **I1a-T** trust gate for v3 (default build) | `trust.c`: selection 5.3, open and handle checks 5.4, writable test 5.5, classification 5.2, crash guard 5.6, store ACL for `Resources\Themes` 5.7, v3 rollback value 5.8, T-LOAD-7 5.9, T-PRIV-4..7; `filesecurity.txt` lines; gate key with its ACL | Jim 2's PASS on this design; 5.11 runtime checks done | policy §11 tests for v3 green in a default build; image test of the ACLs green |
| **I1a-V** v4 load and controls (compiled out by default) | `sdk/lib/tl4` + manifest; `UXTHEME_V4`; 2.3 steps 4–8; dispatch 2.4 (with the `FindPartState` caller audit); images 2.5; range table 6 on every getter; no metrics | I1a-T merged; MSVC build of the TL4 core; build option on in lab only after the S1 report and the range table | section 8 v4 tests green in lab builds; corpus 32/32 in the lab |
| **I1b** getters and metrics | `sysmetrics` through the table; `GetThemeStream`/`GetThemeBitmap`; `GetThemeFilename`; documentation properties; origin semantics checked on Windows | I1a-V | apitests per getter; Windows parity |
| **I1c** install path | staging, `tl4check.exe` sandbox, hashing, `Machine`/`Local`, approval (owner decision), origin files, `StyleName`, applier T-GUARD-6 and T-PRIV-3, removal of `V3AnyPathOrdinary` | Jim 2's review of the facility evidence for 5.10 step 3 | policy §11 install tests green |
| v4 on outside the lab | `EnableV4Ordinary`; `EnableV4Privileged` only with T-GATE-1 | S2 closed; range table in REQUIREMENTS.md; T-GATE-1 | — |
| later, separate | frame painter, atlas images, animations, types 240/241/243 | own design | — |

## 10. Open points

1. `GetThemePropertyOrigin` semantics (I1b, measured on Windows).
2. BCMAP chain vs v3 name-built overrides: parity checked by the corpus test.
3. TrustedInstaller (5.1): re-own the store trees to Administrators (this design), or rule TI trusted (Jim 2).
4. The `%SystemRoot%` literal accepted as a fixed token in today's `DllName` values (5.3) until `StyleName` replaces them in I1c.
5. A model shared between processes (T-LOAD-8): not in I1.

## 11. Review MUSTs (Jim 2, v1) → where answered

| # | MUST | Answered in |
|---|---|---|
| 1 | T-DEF-3/4 definition; T-PRIV-2..7 | 5.2 |
| 2 | v3 checks and crash guard in their own file and switch, active in a default build; byte-identical only for the v4 hooks | `trust.c`, sections 4, 5.6; milestone I1a-T |
| 3 | every process checks v3; the only rollback is an admin-only, default-off, logged ordinary-only value, removed with the installer | 5.8, section 4 |
| 4 | `Machine`/`Local`; TrustedInstaller not trusted without evidence | 5.7; 5.1 (TI evidence and decision) |
| 5 | no path from HKCU; equality, not prefix; system dir from the system; `.mui` language under T-STORE-4 | 5.3, 5.4 step 3, 2.3 step 5 |
| 6 | open first, check on the handle including directories; volume type and ACL support from the handle; FS and ACL enforcement with evidence | 5.4; 5.1 |
| 7 | inverted writable test, all write-class rights, parents per T-DEF-2 | 5.5 |
| 8 | persistent guard before the peek and before v3; pid + start time; part + style; no load if unwritable | 5.6, 2.3 step 3 |
| 9 | range table on every returned value; entry condition for the build gate | section 6, section 0 |
| 10 | install: staging in the store, handle to the checker, whole chain, job limits, hash twice, no overwrite, `Local` unless T-SIG; owner approval | 5.10 |
| 11 | two runtime values plus a test that an ordinary user cannot write the key | section 4, section 8 |
| 12 | generated v4 styles through the store; T-LOAD-7 refusal test; policy §11 tests by id | section 8 |
| 13 | v3 `LoadLibraryExW` reopen only after the location check | 2.3 step 4 |
