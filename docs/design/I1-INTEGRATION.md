# I1: integration of the version-4 loader into LiberNT uxtheme (design)

Author: Stanley (Leeway Open Software), 2026-11-15.

Status: **design v3**:
- after Jim 2's review of v1: architecture accepted, 13 MUSTs answered (section 11);
- after his review of v2: the TrustedInstaller ruling (b) and 7 MUSTs, answered in section 12. Changed sections: 5.1, 5.2, 5.4–5.7, 5.9, 5.11 and 9.

No implementation starts before his PASS.

**Authority:** owner decisions of 2026-11-15:
- (1) R2/I1 authorized: v3 stays operational; v4 is added incrementally behind a controlled boundary, with regression tests and rollback; no wholesale rewrite of uxtheme, Explorer or DWM.
- (2) Windows reference testing authorized in a controlled Windows environment.
- (3) Privileged processes never load theme resources from user-writable locations.
- (4) No GitHub spending increase; Lab01 is evaluated as a local build/test machine.
- (T-SIG-4) An administrator may approve unsigned reference styles into `Machine`.

**Normative documents** (they win over this design):
- `docs/security/REQUIREMENTS.md`, R-PROC-3 as amended in a5c687e;
- `docs/security/THEME-TRUST-POLICY.md` v4 (047e518).

T-* and R-* ids are cited from them.

**Inputs:**
- `docs/design/V4-LOADER-SKETCH.md` (05) §2.2;
- `docs/design/L2-MODEL-DESIGN.md`;
- LiberNT research `research-dwm/02`, `04` and `REPORT-DWM-VISUAL-STYLES.md`.

**Labels:**
- **VERIFIED:** observed, method stated.
- **SOURCE:** path:line. TL4 at main 047e518; LiberNT at public `leeway/main` ad21954.
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

## 5. Trust gate: enforcement of THEME-TRUST-POLICY.md v4

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

**TrustedInstaller (ruling (b), T-DEF-5).**
- The image makes the TI service SID (`S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464`, exact SID only) the owner of the volume root and of the whole `Windows` tree, and gives it full control in `COMPONENTDIR` (`filesecurity.txt:1-4,12,15-17`).
- **Ruling (Jim 2, v2 review):** TI is trusted alongside SYSTEM and Administrators, for owners and for ACE entries, under T-DEF-5. Re-owning would not be enough, because the `COMPONENTDIR` DACL keeps a TI full-control entry.
- **The T-DEF-5 conditions are a HYPOTHESIS until the 5.11 probe:**
  - no ordinary token carries the SID;
  - only SYSTEM or holders of `SeCreateTokenPrivilege`/`SeTcbPrivilege` can make such a token, with any service that would run under it defined in a registry part ordinary principals cannot write.
- **Source facts supporting them:**
  - no TrustedInstaller service is defined (`hivesys.inf:69,209` are `SafeBoot` entries only);
  - the service control manager has no per-service SID support (no `SERVICE_SID_TYPE` handling in `base/system/services`).
- **The flag.** Until the probe and the T-GATE-1 test exist, the integration passes `TL4_TRUST_COMPONENT_OWNER_TRUSTED` (5.5) only from a machine fact that `trust.c` computes once per boot and cannot be set by an ordinary principal (5.11). Where it cannot be computed, the SID is ordinary (T-DEF-5).
- **No ACL changes on existing trees.** `C:\`, `Windows`, `Windows\Resources` and `Resources\Themes` keep the image's descriptors. Only `Styles4` gets new `filesecurity.txt` lines (T-STORE-5, owner Administrators).

### 5.2 Privileged classification (T-DEF-3, T-DEF-4, T-PRIV-*)

`trust.c: UXTHEME_ClassifyProcess()` runs once per process, before the first theme open, on the **primary** token (`OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY)`), never a thread token. The result is cached. The process is privileged when **any** of these holds:

| Clause | Query |
|---|---|
| user is SYSTEM, LocalService or NetworkService | `TokenUser` = S-1-5-18/19/20 |
| Administrators present and enabled (not deny-only) | `TokenGroups`: S-1-5-32-544 with `SE_GROUP_ENABLED` and without `SE_GROUP_USE_FOR_DENY_ONLY` |
| elevated | `TokenElevationType` = `TokenElevationTypeFull`, or `TokenElevation.TokenIsElevated` |
| integrity high or above | `TokenIntegrityLevel` RID ≥ `SECURITY_MANDATORY_HIGH_RID` |
| started by the SCM | the parent is `services.exe` (parent PID from `NtQueryInformationProcess(ProcessBasicInformation)`, image checked through the parent handle, and the parent's start time earlier than ours, so a recycled PID cannot match) |
| session 0 (T-DEF-3 v4) | `TokenSessionId` = 0, **only where interactive logons are kept out of session 0**. Which case holds on LiberNT is taken from the 5.11 probe: the session id of an interactive Explorer and of a service. If interactive users run in session 0, the clause is off (`interactive_in_session0` = 1 in the facts) and the other clauses decide. Until the probe answers, the clause is **on** (strict reading) |
| any listed privilege, enabled or not | `TokenPrivileges` contains one of the ten T-DEF-3 privileges |
| logon or secure desktop | `GetThreadDesktop(GetCurrentThreadId())` → `GetUserObjectInformationW(UOI_NAME)` is `Winlogon` or `Screen-saver`, or the window station is not `WinSta0` |

- **Failure.** Any query that fails means privileged (T-DEF-4, T-FAIL-3).
- **Unit tests.** The classifier is the pure function `tl4_trust_classify` in TL4 `trust/` (commit c4acc16, 213 host checks with the writable test), vendored next to the loader core; `trust.c` only gathers the facts.
- **Rules applied to a privileged process:**
  - **T-PRIV-1:** looks only in `Machine` (5.7).
  - **T-PRIV-2:**
    - reads only the three names, with lengths checked and the name rule applied (5.3);
    - a process whose token user is SYSTEM, LocalService or NetworkService, or that was started by the SCM, reads **no** user setting and uses the machine default (`HKLM\SOFTWARE\Leeway\UxTheme` `MachineDefault`, a style name under T-STORE-4, protected like the gate key). Session 0 alone does not decide this (review v2 MUST 5).
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

**Native calls on the handle (review v2 MUST 7).** `GetFinalPathNameByHandleW`, `GetFileInformationByHandleEx` and `GetVolumeInformationByHandleW` are exported only with `-version=0x600+` (SOURCE: `dll/win32/kernel32/kernel32.spec:530,538,745`). uxtheme imports `ntdll` in both export versions (SOURCE: `dll/win32/uxtheme/CMakeLists.txt:48-51`), so the gate uses the native calls on the handle only.

1. **Open.**
   - The file: `NtOpenFile`/`CreateFileW` with `GENERIC_READ | READ_CONTROL`, share `FILE_SHARE_READ` (write and delete denied to others), `FILE_OPEN_REPARSE_POINT` (`FILE_FLAG_OPEN_REPARSE_POINT`).
   - **A sharing violation on the file is a clean refusal that writes no guard mark** (the gate has not reached 5.6). The process uses the built-in look or the machine default (T-FAIL-1) and does not retry in this process run. Another process holding the style open for writing is an installer or an attacker; neither may make us wait.
   - Each directory from the file's parent up to the volume root: `FILE_READ_ATTRIBUTES | READ_CONTROL` with **full sharing** (`FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`), as a directory (`FILE_FLAG_BACKUP_SEMANTICS`), `FILE_OPEN_REPARSE_POINT`. Full sharing never blocks other processes on system directories. These handles serve the checks below and are closed after 5.4 step 5.
   - All checks below use these handles only.
2. **Type and reparse points.** `NtQueryInformationFile(FileAttributeTagInformation)` on every handle: no `FILE_ATTRIBUTE_REPARSE_POINT`, and the file is not a directory. `NtQueryVolumeInformationFile(FileFsDeviceInformation)`: `FILE_DEVICE_DISK`, and none of `FILE_REMOVABLE_MEDIA`, `FILE_REMOTE_DEVICE` or `FILE_VIRTUAL_VOLUME`.
3. **The path of the handle equals the built path.**
   - `NtQueryObject(ObjectNameInformation)` on the file handle gives the NT name (`\Device\HarddiskVolumeN\Windows\...`).
   - The built path is made the same way: the Windows directory is opened once per process (`SharedUserData->NtSystemRoot` via `GetSystemWindowsDirectoryW`, never `%SystemRoot%` text), its NT name is queried, and the relative built path is appended.
   - The two NT names are compared for **equality** (case-insensitive, whole string). No prefix test: this refuses `Themes2`, 8.3 names, `\\?\` forms and streams.
   - Each directory handle's NT name equals the matching ancestor of the built path.
4. **Volume, from the handle.** `NtQueryVolumeInformationFile(FileFsAttributeInformation)`: `FILE_PERSISTENT_ACLS` set and the file system name `NTFS` (T-STORE-6). The volume is the system volume: the device part of the file's NT name equals that of the Windows directory handle. "Unknown" fails (T-FAIL-3).
5. **Not user-writable (5.5).** `NtQuerySecurityObject(OWNER | DACL)` on the file and on every directory handle up to the store root uses `TL4_TRUST_FILE` / `TL4_TRUST_DIR`; above the store root up to the volume root it uses `TL4_TRUST_DIR_ABOVE_ROOT` (T-DEF-2).
6. **Size and map** from the same file handle (T-LOAD-4): `NtQueryInformationFile(FileStandardInformation)` ≤ 32 MB, then `NtCreateSection(PAGE_READONLY, SEC_COMMIT)` from the handle and a read-only view of exactly that size. Never `SEC_IMAGE`, never `LoadLibrary` for v4.

The `.mui` (and later `shellstyle.dll`) go through 1–6 with their own built paths (T-LOAD-6).

### 5.5 The writable test (T-DEF-1, T-DEF-2, T-DEF-5)

Implemented and host-tested: TL4 `trust/`, `tl4_trust_sd_writable(sd, size, object, flags)` (commit c4acc16).

- **Input.** The self-relative descriptor from `NtQuerySecurityObject`, parsed with bounded reads. NULL, or anything malformed, means writable (`TL4_TRUST_W_UNREADABLE`).
- **Trusted set** for owners and entries: SYSTEM (S-1-5-18), Administrators (S-1-5-32-544), and the component owner TI (exact SID) only with `TL4_TRUST_COMPONENT_OWNER_TRUSTED` (5.1, T-DEF-5). Every other SID is ordinary, other service SIDs included.
- **Writable** when any of these holds:
  - the owner is missing or not trusted;
  - no DACL, or a NULL DACL;
  - an effective (not inherit-only) ACE of an unknown type;
  - an effective allow ACE gives an ordinary principal a write-class right.
- **Write-class rights:**
  - file: `FILE_WRITE_DATA`, `FILE_APPEND_DATA`, `FILE_WRITE_EA`, `FILE_WRITE_ATTRIBUTES`, `DELETE`, `WRITE_DAC`, `WRITE_OWNER`, `MAXIMUM_ALLOWED`, `GENERIC_ALL`, `GENERIC_WRITE`;
  - directory: the same plus `FILE_DELETE_CHILD` (`FILE_ADD_FILE`/`FILE_ADD_SUBDIRECTORY` share the bits of write data/append);
  - above the store root: `DELETE`, `WRITE_DAC`, `WRITE_OWNER`, `FILE_DELETE_CHILD`, `MAXIMUM_ALLOWED`, `GENERIC_ALL`.
- **Deny entries** are ignored.
- **Tests** cover the image's own descriptors: `COMPONENTDIR`/`COMPONENTFILE` are safe with the flag and owner-refused without it; the volume root is safe above the store root and writable as a store directory, because Authenticated Users have `FILE_ADD_SUBDIRECTORY` there.

### 5.6 Crash guard (T-GUARD-1..6), in front of both loaders

- **Key.** `HKCU\Software\Leeway\UxTheme\Guard\<part>-<name>`, one subkey per (store part, style name). `<part>` is `Machine`, `Local`, `Themes` (v3) or `Outside` (5.9). `<name>` follows T-STORE-4; for `Outside`, `<name>` is the first 16 hex digits of the SHA-256 of the built or given path.
- **Value name** = the process: `<pid>-<creation time>`, 8 + 16 hex digits (pid and the `GetProcessTimes` creation time as a 64-bit FILETIME). Concurrent processes never overwrite each other's mark (review v2 MUST 2).
- **Value data:** `REG_BINARY`, **exactly 16 bytes**: `{ uint32 version = 1; uint32 reserved = 0; uint64 written_at (FILETIME) }`.
  - A privileged process reads marks an ordinary one wrote. Any value whose type is not `REG_BINARY`, whose size is not 16, whose version is not 1, or whose name does not parse as `<8 hex>-<16 hex>` counts as a **stale mark**. It makes the style skip, and it is never interpreted further.
  - The subkey's value count is capped at 64 for the scan; more values also count as stale.
- **Order:** read the marks, then write our own value, at 2.3 step 3. That is after the checks on the handle and before the version peek, so before the v3 parser too.
- **Stale.** A value whose pid is not running, or is running with another creation time, makes the process skip the style (T-GUARD-2) and keep the mark.
- **Write failure** means no load (T-GUARD-4).
- **Clean finish.** After a successful load or a clean refusal, the process deletes its own value.
- **Clearing ships in I1a-T (review v2 MUST 3), in the existing apply path.**
  - `ApplyTheme` (`dll/win32/uxtheme/system.c:1271`) deletes the `Guard\<part>-<name>` subkey of the style it applies, after the trust gate passed for it, and before it writes the choice. A deliberate apply is the user's "apply again".
  - The installer (I1c) also clears it on reinstall and removal.
- **Service accounts (SHOULD).** LocalService and NetworkService cannot write the HKLM guard key: a process whose token user is one of them uses its own hive's `HKCU` path, as every other process does. SYSTEM uses `HKLM\SOFTWARE\Leeway\UxTheme\Guard` for the machine default (T-GUARD-5). If the write fails, the built-in look (T-GUARD-4).
- **T-GUARD-6:** the applier (I1c) performs the first load of a newly chosen style in its own process before writing the choice.

### 5.7 The store (T-STORE-1..8, T-PRIV-1)

| Item | Location | Notes |
|---|---|---|
| store root (v4) | `<GetSystemWindowsDirectoryW>\Resources\Styles4` | |
| parts | `…\Styles4\Machine`, `…\Styles4\Local` | a style is in exactly one part (T-STORE-2) |
| layout | `<part>\<name>\<name>.msstyles`, `<part>\<name>\<language>\<name>.msstyles.mui`, `<part>\<name>\Shell\<colour>\shellstyle.dll` | fixed (T-STORE-3) |
| v3 location (T-STORE-8) | `<windir>\Resources\Themes\<name>\<name>.msstyles` | the only v3 path; counts as `Machine` only while 5.4–5.5 pass |
| ACL (T-STORE-5) | `O:BAG:SYD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;AU)` on `Styles4` and both parts **only** | new `filesecurity.txt` lines through `ntfsimg --apply-security`; read back by the image test (T-STORE-7). `Resources\Themes` keeps the image's `COMPONENTDIR`/`COMPONENTFILE` descriptors (owner TI, trusted under T-DEF-5, 5.1) |

- **Part choice.** A privileged process looks only in `Machine`, and in `Resources\Themes` for v3 (T-PRIV-1). An ordinary process looks in `Machine`, then `Local`.
- **One name, one place (SHOULD).** The installer refuses a style name that already exists in any of `Resources\Themes`, `Styles4\Machine` or `Styles4\Local`. If a duplicate is found anyway, a `StyleName` selection resolves in the fixed order `Resources\Themes` (v3), then `Styles4\Machine`, then `Styles4\Local` (ordinary processes only), and the duplicate is logged.
- **v3 has no `Local`.** Every v3 style counts as `Machine` while it passes the checks.

### 5.8 v3 in ordinary processes, and its only rollback (T-GATE-4, review MUST 3)

- 5.3–5.6 apply to the v3 loader in **every** process from I1a on.
- The only exception is `V3AnyPathOrdinary` = 1 (section 4). With it, an **ordinary** process loads v3 styles as before I1 (any `DllName` path, `LoadLibraryExW`, no location check), and logs each such load (one event per style per process).
- A privileged process never reads that value.
- The value is removed (code and setup) in the release that ships the installer (I1c).

### 5.9 Caller-given file names (T-LOAD-7; review v2 MUST 6)

`OpenThemeFile`, `OpenThemeDataFromFile`, `GetThemeDefaults`, `CheckThemeSignature` (`system.c`):
- **Inside the store** (the name equals a built store path, 5.3 rule): 5.4–5.6 as for the selected style, in every process.
- **Outside the store, privileged process:** refused (`E_ACCESSDENIED`, T-PRIV-7 event).
- **Outside the store, ordinary process, v3:** refused, unless `V3AnyPathOrdinary` = 1 (5.8). With the rollback on, it loads as before I1 and is logged.
- **Outside the store, ordinary process, v4 (only with `EnableV4Ordinary`):**
  - 5.4 steps 1, 2 and 6 (single open, type, data mapping), then full validation;
  - guarded under `Guard\Outside-<hash>` (5.6);
  - logged.

  The location rules of 5.4 steps 3–5 do not apply here, because the caller chose the file. This is the T-LOAD-7 SHOULD kept for compatibility, and it exists only for v4, which has the bounded parser.
- The version peek (2.3 step 4) routes a file outside the store. A v3 file found there is refused, unless the rollback is on.
- **Apitests that load a style from a temporary path (SHOULD):** I1a-T lists every one in `modules/rostests` and `winetests` (known candidates: `apitests/uxtheme/ThemeRendering.c`, `ThemeDpi.c`, `GetThemeParseErrorInfo.c`, and `winetests/uxtheme/system.c` with `OpenThemeFile`). Each one is changed to install its style into the store with the T-GATE-5 test helper, or it is marked as testing the refusal.

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
  - the SIDs of the running service tokens (the TI question);
  - the session id of an interactive Explorer and of a service (the session 0 clause, 5.2).
- The native calls of 5.4 on the shipped NTFS driver (`ntfs_new`) and the shipped volume, from an ordinary and an elevated process: `NtQueryObject(ObjectNameInformation)`, `FileAttributeTagInformation`, `FileFsDeviceInformation`, `FileFsAttributeInformation` (it must report `FILE_PERSISTENT_ACLS` and `NTFS`), `NtQuerySecurityObject` on a file and a directory, and `NtCreateSection` from a read-only handle.
- **T-DEF-5 machine fact.** A boot-time check computes the fact behind `TL4_TRUST_COMPONENT_OWNER_TRUSTED` (5.1), run by `trust.c` from SYSTEM context (winlogon start) and stored in a volatile HKLM value that only SYSTEM can write. It verifies:
  - no process token on the system (`NtQuerySystemInformation(SystemProcessInformation)` + `NtOpenProcessToken`) carries the TI SID as user or group;
  - no `Services\TrustedInstaller` key exists, or it is owned by SYSTEM/Administrators.

  If it is absent or cannot be read, the SID is ordinary. The T-GATE-1 test (an ordinary process token carrying a service SID must fail it) is part of section 8.

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
| Host unit tests: `tl4_trust_sd_writable` and `tl4_trust_classify` (TL4 `trust/`, c4acc16) | every T-DEF case; 213 checks pass with gcc -Werror and clang ASan/UBSan |
| Two concurrent loads of the same style (two processes started together) | both load; two guard values; both removed |
| Sharing violation: a process holds the style open for writing | clean refusal, no guard mark, built-in look |
| TI-owned style (`Resources\Themes`) in a privileged process, with and without the T-DEF-5 fact | loads with it; refused without it |
| Ordinary process token carrying a service SID (T-GATE-1) | the T-DEF-5 fact is false |
| Reference corpus (lab VM only, `TL4_CORPUS_DIR`, never committed): the 32 styles approved into `Machine` in the VM | 32/32 load; values equal the TL4 corpus dump |
| Windows parity: controlled Windows VM, stock `aero.msstyles`, no patchers (owner decision 2) | Windows build recorded; differences listed |
| Performance (lab) | load time and memory per process, recorded per commit |

## 9. Milestones

| Milestone | Content | Entry | Exit |
|---|---|---|---|
| **I1a-T** trust gate for v3 (default build) | `trust.c`: selection 5.3, native handle checks 5.4, writable test 5.5 and classifier 5.2 (vendored TL4 `trust/`), crash guard 5.6 **with its clearing in `ApplyTheme`**, v3 rollback value 5.8, T-LOAD-7 5.9, T-PRIV-4..7, the T-DEF-5 boot fact 5.11; `filesecurity.txt` lines for `Styles4` only; gate key with its ACL | Jim 2's PASS on this design; 5.11 runtime checks done | policy §11 tests for v3 green in a default build; image test of the ACLs green |
| **I1a-V** v4 load and controls (compiled out by default) | `sdk/lib/tl4` + manifest; `UXTHEME_V4`; 2.3 steps 4–8; dispatch 2.4 (with the `FindPartState` caller audit); images 2.5; range table 6 on every getter; no metrics | I1a-T merged; MSVC build of the TL4 core; build option on in lab only after the S1 report and the range table | section 8 v4 tests green in lab builds; corpus 32/32 in the lab |
| **I1b** getters and metrics | `sysmetrics` through the table; `GetThemeStream`/`GetThemeBitmap`; `GetThemeFilename`; documentation properties; origin semantics checked on Windows | I1a-V | apitests per getter; Windows parity |
| **I1c** install path | staging, `tl4check.exe` sandbox, hashing, `Machine`/`Local`, approval (owner decision), origin files, `StyleName`, applier T-GUARD-6 and T-PRIV-3, removal of `V3AnyPathOrdinary` | Jim 2's review of the facility evidence for 5.10 step 3 | policy §11 install tests green |
| v4 on outside the lab | `EnableV4Ordinary`; `EnableV4Privileged` only with T-GATE-1 | S2 closed; range table in REQUIREMENTS.md; T-GATE-1 | — |
| later, separate | frame painter, atlas images, animations, types 240/241/243 | own design | — |

## 10. Open points

1. `GetThemePropertyOrigin` semantics (I1b, measured on Windows).
2. BCMAP chain vs v3 name-built overrides: parity checked by the corpus test.
3. TrustedInstaller: ruled (b), trusted under T-DEF-5 (5.1); the conditions are shown by the 5.11 probe and boot fact.
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

## 12. Review v2 MUSTs (Jim 2, c770431) → where answered

| # | MUST | Answered in |
|---|---|---|
| 1 | trusted set SYSTEM, Administrators, TI for owners and entries; no re-owning; only `Styles4` gets new lines | 5.1, 5.5, 5.7 |
| 2 | guard value name with pid and start time; fixed-size, type-checked data | 5.6 |
| 3 | stale-mark clearing in I1a-T, in `ApplyTheme` (`system.c:1271`) | 5.6, section 9 |
| 4 | parents opened with full sharing; a sharing violation on the file is a clean refusal without a mark | 5.4 step 1 |
| 5 | session 0 from the probe; "reads no user setting" only for service users and SCM starts | 5.2, 5.11 |
| 6 | v3 outside the store refused in every process unless the rollback is on; only v4 outside the store loads in an ordinary process, with its guard mark | 5.9, 5.6 (`Outside`) |
| 7 | `-version=0x600+` exports: native calls on the handle instead, and lab check | 5.4, 5.11 |

The SHOULDs are answered as follows:
- `StyleName` order and installer refusal of duplicates: 5.7.
- Service accounts and the guard: 5.6.
- Apitests that load from a temporary path: 5.9.
- Concurrent loads, sharing violation, a TI-owned style in a privileged process, and the T-GATE-1 service-SID test: section 8.

## 13. Notes for I1a-T (Jim 2, design v3 PASS)

- **Process identity (5.2, 5.6).** The stale-mark test and the parent check take pid and creation time from one `NtQuerySystemInformation(SystemProcessInformation)` snapshot. "Cannot open" never means "gone". A parent that no longer exists, or whose creation time is not earlier than ours, is not the SCM.
- **The T-DEF-5 fact (5.1, 5.11).** The fact lives in the volatile subkey `ComponentOwner` of the gate key. Every reader applies the writable test to the gate key and to that subkey, and treats a failure as "TI is ordinary". The `Services\TrustedInstaller` check is "absent, or not user-writable by that same test".
- **Handle names (5.4 step 3).** The object name of a handle is the name after any reparse, and it is not normalized. 8.3 names and streams are excluded by the name rule (no `~`, no `:` can come out of a built path). The section 8 junction and symlink tests must show this on `ntfs_new`.
- **Run-as tokens (5.2).** A `LogonUser` (run-as) token of a standard user has session id 0 on LiberNT (I1-ACL-TOKEN-EVIDENCE). Such a process is classified privileged and gets only `Machine` styles. That is the safe side, and intended.
- **T-PRIV-6** stays in the integration (logon process, `Winlogon` or `Screen-saver` desktop, or a window station other than `WinSta0` uses the machine default), with its own test. `tl4_trust_uses_machine_default` covers T-PRIV-2 only.
- **Version peek and R-PE-8 (open, 2026-11-15).** The shipped v3 `aero.msstyles` has `RT_STRING` blocks in 14 languages. The bounded PE reader refuses it under R-PE-8, so the version peek of 2.3 step 4 cannot use the reader unchanged. A ruling is pending: amend R-PE-8 for the general reader, use a separate peek, or change the T-LOAD-2 wording.
- **5.11 closed (Jim 2, round 2 evidence, int41 b1e08ed9d95):**
  - **Boot fact (T-DEF-5).** It is computed by SYSTEM with `SeDebugPrivilege` enabled. It is true only when the token of every process in one snapshot was opened and read (pid 0 excepted). A single process that cannot be opened makes it false (T-FAIL-3). Dwight checks that SYSTEM can open every process on LiberNT before the code relies on it. The first I1a-T cut also skipped pid 4 and did not enable `SeDebugPrivilege`; it changes to this rule.
  - **Persistent ACLs.** `ntfs_new` enforces ACLs but did not report `FILE_PERSISTENT_ACLS` (FileFsAttributeInformation 0x00a42087). The check stays strict, with no fallback on the name `NTFS`. The driver is fixed instead (`ntfs_new` vol.c:236). Until then the gate fails closed, and the I1a-T tests need the fixed image.
  - **Version peek (R-PE-8, ruling A, TL4 d2bc67b).** 1 to 64 language entries per name, the lowest language id wins, and the v4 validator stays strict. `PACKTHEM_VERSION` must have exactly one language entry, or the file is refused for both loaders. The shipped v3 style has one (VERIFIED: id 1, language 0x0); only its `RT_STRING` blocks have 14.
