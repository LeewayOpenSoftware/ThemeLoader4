# Theme trust policy

Status: policy, version 6 (2026-11-15; version 6 after the review of the first trust gate code: colour and size names in T-LOAD-1, the deliberate open in T-GUARD-2, registry keys in T-DEF-1; version 5 after the review of the trust code c4acc16: the Administrators clause of T-DEF-3; version 4 after the review of the I1 design v2: the system component owner as a trusted principal with T-DEF-5, the session 0 clause of T-DEF-3, the `%SystemRoot%` token in T-LOAD-1; version 2 after the review of the I1 design: T-LOAD-2, T-FAIL-1, T-GATE-2 to T-GATE-5; version 3 records the owner decision on T-SIG-4). Owner: security review. Enforcement is designed in the integration milestone I1 and is not enabled before the gate in section 10 is met.

This file turns one owner decision into rules that code and tests can cite:

> Elevated or privileged processes must not load theme resources from locations writable by unprivileged users. Themes are installed by an administrator into protected locations, validated, and carry a signature policy.

`REQUIREMENTS.md` stays the single normative list for the core; its section 7 (R-POL-1 to R-POL-7) points here. Where this file and an integration design disagree, this file wins and the design is corrected. A rule changes only by a commit here that says why.

- **MUST** is mandatory. **SHOULD** needs a written reason when it is not followed.
- Evidence labels: **SOURCE** (read in a source tree, path given), **HYPOTHESIS** (not checked). Nothing in this file was measured on a running system.

## 1. What the policy protects

- The parser is written to be safe on any input (`REQUIREMENTS.md` section 1), and S1 and S2 test that. This policy is the second layer: it decides **where a style may come from and who may make it active**, so that a defect that the tests missed cannot be reached by an unprivileged user in a process that has more rights than that user.
- It also removes the attacks that need no parser defect: swapping the file between the check and the use, pointing the loader at a share, and locking a user or a service out with a style that crashes.
- A signature is never the safety mechanism (section 7).

The rules apply to **every loader that reads a style**: the version-3 loader that stays in service and the version-4 loader. A rule that only the new loader obeys does not meet the owner decision, because the old loader reads the same per-user setting (SOURCE: LiberNT `dll/win32/uxtheme/system.c:149-200`, the path comes from `HKCU\...\ThemeManager` and is used as read).

## 2. Terms

**Theme resource.** Any file whose bytes a process parses because of the theme choice: the style (`.msstyles`), its string file (`.mui`), `shellstyle.dll`, and what a `.theme` file names (wallpaper, cursors, sounds, icons).

**Trusted principals.** `SYSTEM` (S-1-5-18), the `Administrators` group (S-1-5-32-544) and, under T-DEF-5, the system component owner `TrustedInstaller` (S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464), which the system image sets as owner of the system directory tree. Every other SID is an **ordinary principal**, including `LocalService`, `NetworkService`, `Users`, `Authenticated Users` and `Everyone`.

**Write rights.** For a file: `FILE_WRITE_DATA`, `FILE_APPEND_DATA`, `FILE_WRITE_EA`, `FILE_WRITE_ATTRIBUTES`, `DELETE`, `WRITE_DAC`, `WRITE_OWNER`, and the generic rights that contain them. For a directory, also `FILE_ADD_FILE`, `FILE_ADD_SUBDIRECTORY` and `FILE_DELETE_CHILD`.

| Id | Rule |
|---|---|
| T-DEF-1 | A file or directory is **user-writable** when any of these holds: its owner is not a trusted principal; it has no DACL or a NULL DACL; an allow entry gives a write right to an ordinary principal; its security descriptor cannot be read. A deny entry is never used to argue that an object is safe. For a registry key the same test applies with the write rights of a key: set value, create subkey, create link, delete, change of DACL or owner, and the generic write rights; the right to be notified of changes is not a write right. |
| T-DEF-2 | A **location** is user-writable when any of these holds: the file, or any directory from the file up to and including the store root, is user-writable (T-DEF-1); a directory above the store root, up to the volume root, has an owner that is not a trusted principal, or gives an ordinary principal `DELETE`, `WRITE_DAC`, `WRITE_OWNER` or `FILE_DELETE_CHILD`; any component of the path is a reparse point (junction, symbolic link, mount point); the volume is not a local fixed volume (a share, removable or optical media, a RAM or virtual disk mounted by a user); the file system of the volume does not store security descriptors (FAT, exFAT). |
| T-DEF-3 | A process is **privileged** when any of these holds for its primary token: the user is `SYSTEM`, `LocalService` or `NetworkService`; the `Administrators` group is present and not deny-only, whether it is enabled at the moment or not (a process can enable a group that is only disabled); the token is elevated (elevation type full, where the system has split tokens); the integrity level is high or above (where the system has integrity levels); the process was started by the service control manager, or runs in session 0 (the session clause only where the system keeps interactive logons out of session 0; where an interactive user runs in session 0 the clause is not used and the other clauses decide); the token holds, enabled or not, one of `SeTcbPrivilege`, `SeDebugPrivilege`, `SeImpersonatePrivilege`, `SeAssignPrimaryTokenPrivilege`, `SeCreateTokenPrivilege`, `SeLoadDriverPrivilege`, `SeBackupPrivilege`, `SeRestorePrivilege`, `SeTakeOwnershipPrivilege`, `SeSecurityPrivilege`; it is the logon process or runs on the logon or secure desktop. |
| T-DEF-5 | The component owner SID is a trusted principal only as long as both hold: no process that T-DEF-3 calls ordinary has that SID in its token, as user or as group; and a token with that SID can be made only by `SYSTEM` or by a holder of `SeCreateTokenPrivilege` or `SeTcbPrivilege`, with any service that would run under it defined in a part of the registry that ordinary principals cannot write. Reason: the SID is then no weaker than `Administrators`, who can take ownership of any object anyway, and trusting it keeps the owner of the volume root and of the system directory as the image ships them. No other service SID (S-1-5-80-…) and no other SID is trusted by this rule. Where a check of this rule cannot be made, the SID is ordinary. The store root, its parts and everything below them keep T-STORE-5 (owner `Administrators`). |
| T-DEF-4 | The classification is made once per process, from the primary token, before any theme resource is opened. When any query it needs fails, the process is privileged. A thread that impersonates never changes the result. |

Whether LiberNT has split tokens and integrity levels today is a HYPOTHESIS. T-DEF-3 is written so that it is correct either way: on a system without them, every process of an administrator is privileged.

## 3. The protected store

| Id | Rule |
|---|---|
| T-STORE-1 | MUST: every style, `.mui` and `shellstyle.dll` that any process loads lives in the store. The store root is a directory below the system directory (proposal for I1: `%SystemRoot%\Resources\Styles4`). The system directory is taken from the system (the kernel's shared data or `GetSystemWindowsDirectoryW`), never from an environment variable and never from a per-user setting. |
| T-STORE-2 | MUST: the store has two parts. `Machine` holds styles that privileged processes may load (section 5). `Local` holds styles an administrator installed for ordinary processes only. A style is in exactly one part. |
| T-STORE-3 | MUST: the layout below a part is fixed and written by the installer, never taken from a package: `<part>\<name>\<name>.msstyles`, `<part>\<name>\<language>\<name>.msstyles.mui`, `<part>\<name>\Shell\<colour>\shellstyle.dll`. |
| T-STORE-4 | MUST: a style name matches `[A-Za-z0-9][A-Za-z0-9_-]{0,31}`. `<language>` matches `[a-z]{2,3}(-[A-Za-z0-9]{2,8}){0,2}`. `<colour>` follows the name rule. Nothing else is a valid name: no separator, no dot, no space, no device name. |
| T-STORE-5 | MUST: the store root, both parts and everything below them are owned by `Administrators`, with a protected DACL (no inheritance from above) that gives full control to `SYSTEM` and `Administrators`, and read and traverse to `Authenticated Users`, inherited by files and directories. In SDDL: `O:BAG:SYD:PAI(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1200a9;;;AU)`. No other entry. |
| T-STORE-6 | MUST: the store exists only on a volume whose file system stores security descriptors. On any other system volume there is no protected location, so T-PRIV-1 leaves every privileged process on the built-in look, and the installer refuses to install. |
| T-STORE-7 | MUST: the styles shipped with the system are placed in `Machine` by the system's own setup, with the ACL of T-STORE-5 set by setup and checked by a test of the installed image. |
| T-STORE-8 | MUST, for the version-3 loader while it stays in service: the existing system theme directory (`%SystemRoot%\Resources\Themes`) is the only place it loads from, and it counts as `Machine` only while it is not user-writable (T-DEF-2); otherwise privileged processes load nothing from it. Whatever is in it was put there by setup or by an administrator; once the installer of section 6 exists, new styles enter it only through that installer and section 7. |

Which file system the LiberNT system volume uses, and whether file and registry ACLs are enforced there as on Windows, is a HYPOTHESIS and the first thing I1 must state with evidence. T-STORE-6 says what happens when the answer is no.

## 4. Rules for every process at load time

These hold for ordinary and privileged processes alike. They are the load side of R-POL-1 to R-POL-4.

| Id | Rule |
|---|---|
| T-LOAD-1 | MUST: the per-user setting selects a style **by name only** (T-STORE-4), plus a colour and a size name. A colour or size name is text of at most 64 characters with its end checked; it is only compared for equality with the names the checked style itself lists, and so may contain spaces (the shipped styles use "Normal Color"). Where such a name becomes part of a path (`<colour>` in T-STORE-3), it must match the name rule of T-STORE-4. The loader builds the path itself from the store root, the part and the names. A setting that holds a path, an environment reference or anything that does not match the name rule selects nothing. For the settings that exist today (`DllName` with a full path), the value is accepted only when, taken as text and without expanding anything, it is exactly a path the loader would have built; otherwise it selects nothing. The literal text `%SystemRoot%` at the start of such a value is accepted as a fixed token that stands for the system directory of T-STORE-1; it is compared, never expanded. |
| T-LOAD-2 | MUST: the file is opened once, read-only, without following a reparse point on the last component, with a share mode that denies writing and deleting. Every later step uses that handle: the checks, the size, and the mapping (R-POL-2). No step opens the path a second time. One exception while the version-3 loader is in service: when the checks of T-LOAD-3 passed on the handle and the bounded reader found version 3, the version-3 loader may open the same path once more with its existing data-file load. The exception rests on the location not being user-writable, so it does not exist where T-GATE-4 switched the checks off. |
| T-LOAD-3 | MUST, on the handle and before any byte of the file is parsed: it is a regular disk file; the final path of the handle equals the path the loader built; no component is a reparse point; the volume is local and fixed and stores security descriptors; the location is not user-writable (T-DEF-2), checked on the file through the handle and on each directory. Any failure is a reject. |
| T-LOAD-4 | MUST: the file is mapped as data, read-only, never as an image that can run and never through a library load that runs code or applies relocations. The mapped size is the size that was checked. |
| T-LOAD-5 | MUST: the style is validated again in the loading process on every load (R-GEN-10). The store, the installer's validation and the origin mark relax nothing in the parser. |
| T-LOAD-6 | MUST: the `.mui` and `shellstyle.dll` of a style are opened under T-LOAD-2 to T-LOAD-5 as well, from the fixed layout of T-STORE-3. No MUI redirection and no search path is used to find them. |
| T-LOAD-7 | MUST: an API that takes a file name from its caller (`OpenThemeFile` and the like) is under the same rules in a privileged process: a name outside the store is refused. In an ordinary process it SHOULD be refused too; where compatibility needs it, the file is still opened under T-LOAD-2 and T-LOAD-4 and fully validated. |
| T-LOAD-8 | MUST: a style received from another process (a shared section, a handle, a message) is untrusted input like a file. A privileged process maps such a section only when it was created by a `SYSTEM` service, is read-only for everyone else, and its content came from the store under these rules; it still validates it (T-LOAD-5). |

## 5. Additional rules for privileged processes

| Id | Rule |
|---|---|
| T-PRIV-1 | MUST: a privileged process loads theme resources only from the `Machine` part of the store. A selected name that is not in `Machine` selects nothing there, even when it exists in `Local`. |
| T-PRIV-2 | MUST: a privileged process reads from the user's settings only the three names of T-LOAD-1, and treats them as hostile text: length checked before use, name rule applied, never used in a format string or a path except through T-LOAD-1. A process whose own profile is not an interactive user's (`SYSTEM`, services) uses the machine default and reads no user's settings. |
| T-PRIV-3 | MUST: a privileged process never opens a file named by a `.theme` file, and never loads a wallpaper, cursor, sound or icon because a per-user theme setting names it, unless that file is itself in a location that is not user-writable (T-DEF-2). |
| T-PRIV-4 | MUST: theme resources are opened with the process token. When the calling thread impersonates, the loader does not load and does not cache a result; the process keeps the look it had. |
| T-PRIV-5 | MUST: a theme-change notification from another process only makes a privileged process run sections 4 and 5 again. It carries no path and no data that the process uses. SHOULD: at most one reload in 5 seconds. |
| T-PRIV-6 | MUST: the logon screen and the secure desktop use the machine default only. |
| T-PRIV-7 | MUST: a privileged process shows no dialog and asks no question when a style is refused. It writes one event with a fixed text and the style name after the name rule was applied. |

Consequence to know: an administrator who installs a style only into `Local` sees it in ordinary windows and not in elevated ones, which then use the machine default. That is the intended behaviour, not a defect.

## 6. Installing a style

Installing is an administrator action (R-POL-1). Choosing among installed styles is a user action.

| Id | Rule |
|---|---|
| T-INST-1 | MUST: the package the user supplies is untrusted and may sit in a user-writable place. The installer first copies each file it will consider into a staging directory inside the store root whose ACL allows only trusted principals, and works on that copy from then on. The source is never read again. |
| T-INST-2 | MUST: the installer accepts only these kinds of file: a style, its `.mui` files, and a `shellstyle.dll`; the last only from the milestone at which the DirectUI parser's own campaign and review are closed (R-DUI-18). Every other file in a package is ignored and reported. An executable, a driver, a patched system file, a script, an installer or a registry file from a package is never installed, run or registered (R-POL-6). |
| T-INST-3 | MUST: size limits of `REQUIREMENTS.md` section 6 are applied to the source before the copy is made. |
| T-INST-4 | MUST: each staged file is validated in a separate process that is thrown away afterwards: an ordinary restricted token (no administrator group, no privilege beyond the minimum), the lowest integrity level the system offers, a job with a memory limit, a CPU time limit and no child processes, no window station access beyond what the process needs to start, no network use. It receives a read-only handle to the staged copy, not a path. It reports through its exit code only. |
| T-INST-5 | MUST: the validation is the whole chain, with the same code the loaders use: the bounded PE reader, the version-4 validator, the model build, every image and stream through the chunk walker (R-RES-3), every string table, and for `shellstyle.dll` the UIFILE parser. SHOULD: every image is also decoded once in that process, under the pixel budget (R-RES-4). A crash, a timeout or a non-zero exit is a reject. |
| T-INST-6 | MUST: the installer computes the SHA-256 of the staged copy before the validation and again after it, and installs only when both are equal. It then moves the copy into its place in the store on the same volume, under the names of T-STORE-3 and T-STORE-4 chosen by the installer. A name that exists is not overwritten; replacing a style is remove, then install. |
| T-INST-7 | MUST: a rejected style is deleted from staging, never offered and never written to a user's settings (R-POL-3). The staging directory is emptied when the installer starts. |
| T-INST-8 | MUST: a style goes to `Local` unless section 7 allows `Machine` for it. Moving a style from `Local` to `Machine` later is a separate, explicit administrator action that states the origin mark it relies on. |
| T-INST-9 | MUST: removing a style is an administrator action. A user's setting that names a removed style selects nothing (T-LOAD-1), and the process uses the machine default. |

## 7. Signature policy: a mark of origin

| Id | Rule |
|---|---|
| T-SIG-1 | MUST: a signature or a hash match never replaces a check. It is not read by the parser, it relaxes no rule of sections 3 to 6 and no requirement of `REQUIREMENTS.md`. It answers one question only: may this style be placed in `Machine`. |
| T-SIG-2 | MUST: every installed style has one origin mark, decided at install and recorded by the part of the store it is placed in and in the installer's log: **system** (the SHA-256 of each of its files is in the list shipped with the system release), **signed** (a valid signature by a publisher the machine policy names), **approved** (no such evidence; an administrator approved it for privileged processes by the explicit action of T-INST-8), **unsigned** (none of these). |
| T-SIG-3 | MUST: `Machine` holds only styles marked system, signed or approved. Unsigned styles go to `Local`. |
| T-SIG-4 | MUST: the machine policy is read from a machine-wide setting that ordinary principals cannot write, and has a built-in default used when the setting is absent or unreadable. Default: system styles go to `Machine`; signed styles go to `Machine` only for publishers the policy lists (none by default); approval by an administrator is allowed (owner decision 2026-11-15: an administrator may approve an unsigned style, which then may enter `Machine` and be loaded by privileged processes, subject to every other rule of this file; a signing scheme is left for later). An administrator can switch approval off, and then only system and listed publishers reach `Machine`. |
| T-SIG-5 | MUST: until a signature verifier exists in the system and has been reviewed, the mark "signed" is not used; the hash list (system) and the approval are the only ways into `Machine`. The signature that Windows appends to a style is not evidence of anything here and is never checked. |
| T-SIG-6 | SHOULD: the approval action shows the administrator the file name, the SHA-256 and the plain statement that elevated programs will parse this file. |

Why approval exists: the styles the owner wants to use are third-party files with no usable signature. Without approval they could never appear in elevated windows. With it, the decision to expose privileged processes to a given file is taken by an administrator, once, for a file that validated, and is visible in the store.

## 8. Failing closed

| Id | Rule |
|---|---|
| T-FAIL-1 | MUST: when any rule of sections 2 to 5 or any validation fails, the process uses the built-in unthemed look, or the machine default style when that one passes the same rules. It never uses part of the refused style, never tries a second per-user choice, and never retries the same style in the same process run. |
| T-FAIL-2 | MUST: a refusal changes nothing outside the process: no setting is rewritten, no file is touched (R-GEN-5, R-POL-4). |
| T-FAIL-3 | MUST: "unknown" is a refusal. A check that cannot be made (the security descriptor cannot be read, the volume type cannot be told, the token cannot be queried) counts as failed, and for T-DEF-4 as privileged. |
| T-FAIL-4 | MUST: the version-3 loader and the version-4 loader are behind the same checks of sections 4 and 5. A style that the version-4 path refuses is not handed to the version-3 path, and the other way round. |

## 9. Crash guard

This is R-POL-5 made precise. Its purpose is that a style that crashes a process cannot lock a user out of the desktop or put a service into a restart loop.

| Id | Rule |
|---|---|
| T-GUARD-1 | MUST: before the first byte of a style is parsed, the process records a mark that names the style (part and name), the process (id and start time) and survives the death of the process and a restart of the system. The mark is removed when the load has finished, successfully or with a clean refusal. |
| T-GUARD-2 | MUST: before loading, a process looks for marks of the same style left by processes that no longer exist. One such mark makes it skip the style and use T-FAIL-1. The skip lasts until the user applies a style again on purpose, or an administrator reinstalls or removes the style. The skip binds the loads a process makes on its own (at start and on a change notification). A program that opens a style because its user asked for it (the open and apply calls of the applier) does not skip: it loads under its own mark, and a successful apply removes the stale marks. Otherwise a skipped style could never be applied again. |
| T-GUARD-3 | MUST: the mark lives in the settings of the process's own token user. A privileged process honours a mark it finds (it only makes the process skip) and gives no other meaning to it. That an ordinary process of the same user can delete the mark is accepted: the style it re-exposes is one from `Machine`, which T-PRIV-1 already allowed. |
| T-GUARD-4 | MUST: when the mark cannot be written, the style is not loaded. |
| T-GUARD-5 | MUST: the logon screen needs no guard, because it uses the machine default only (T-PRIV-6); when the machine default itself leaves a stale mark, every process uses the built-in look until an administrator acts. |
| T-GUARD-6 | SHOULD: the first process of a session to load a newly applied style is the one that applies it (the control panel), before the choice is written to the settings; a crash there never reaches the shell. |

## 10. Gate: before the version-4 path is enabled

| Id | Rule |
|---|---|
| T-GATE-1 | MUST: no privileged process uses the version-4 loader before all of these are true: the S1 and S2 campaign reports are written and their findings closed (R-PROC-3); the enforcement code of sections 2 to 9 was reviewed by the security reviewer; the tests of section 11 pass on the build that ships; I1 has stated with evidence how the system volume stores and enforces ACLs; the two conditions of T-DEF-5 are shown on a running system, with a test in section 11 that fails when an ordinary process token carries a service SID. |
| T-GATE-2 | MUST: until then the integration keeps privileged processes on the version-3 path or the built-in look. The version-3 path gets T-LOAD-1, T-LOAD-3, the crash guard of section 9 and, in privileged processes, section 5, in front of its first read of the file. |
| T-GATE-3 | MUST: enabling the version-4 path is a switch that an administrator can turn back (rollback), stored where ordinary principals cannot write. Ordinary and privileged processes have separate switches; the one for privileged processes is off until T-GATE-1 is met. |
| T-GATE-4 | MUST: the checks in front of the version-3 loader do not depend on the build option or the switch of the version-4 path: a build without version 4 still has them. For ordinary processes only, a machine-wide setting that ordinary principals cannot write may put the version-3 path back to its old behaviour (any path) as a rollback. Default: the checks are on. The setting never applies to a privileged process, each such load is logged, and the setting is removed when the installer of section 6 ships. |
| T-GATE-5 | MUST: no shipped binary has a way around these rules for tests. A test that needs a style puts it into the store the way an administrator would, or uses a test build that is never shipped. |

## 11. Tests the enforcement must have

Each is a negative test unless it says otherwise, and each cites the rule id.

- Classification (T-DEF-3, T-DEF-4): one case per clause, and a failing token query gives "privileged".
- Location (T-DEF-1, T-DEF-2, T-LOAD-3): a file with a write entry for `Users`; a file owned by a user; a NULL DACL; a parent directory with `FILE_ADD_FILE` for `Users`; a junction and a symbolic link at each level; a share path; a removable volume; a FAT volume.
- Selection (T-LOAD-1, T-STORE-4): a full path, a relative path, `..`, a device name, an environment reference, an over-long value, a name with a trailing dot or space, a name of another style's directory with a separator.
- Swap (T-LOAD-2): the file is replaced after the open; the loader still reads the bytes it checked, and the replacement fails because of the share mode.
- Privileged (T-PRIV-1 to T-PRIV-5): a style present only in `Local`; a `.theme` naming a user file; a load attempted under impersonation; a burst of change notifications.
- Install (T-INST-1 to T-INST-8): a package with an executable; a source changed after the copy; a style that makes the validator crash, hang, and exceed its memory limit; an existing name; an unsigned style asked into `Machine` with approval switched off.
- Fail closed (T-FAIL-1 to T-FAIL-4): after each refusal above, the settings and the store are byte-identical and the process has the default look; a style refused by one loader is not loaded by the other.
- Crash guard (T-GUARD-1 to T-GUARD-4): a process killed during the load leaves a mark; the next process skips the style; applying again clears the skip; an unwritable mark means no load.
- Positive: a system style in `Machine` loads in an ordinary and in a privileged process; an approved style loads in both; an unsigned style in `Local` loads in an ordinary process only.

## 12. Not verified, and decisions that are not mine

- HYPOTHESIS: whether LiberNT has split tokens, integrity levels, a secure desktop, job limits and restricted tokens that work as T-DEF-3 and T-INST-4 assume. Where one is missing, the rule keeps its strict reading (the process is privileged; the validator runs with the least rights the system can give) and I1 lists the gap.
- HYPOTHESIS: the file system of the system volume and the enforcement of file and registry ACLs (T-STORE-6).
- HYPOTHESIS: that a signature verifier usable for T-SIG-2 "signed" exists.
- Not covered: wallpapers, cursors, sounds and icons in ordinary processes. Their parsers are outside this repository; T-PRIV-3 keeps privileged processes away from them.
- Decided by the owner on 2026-11-15: approval by an administrator (T-SIG-2, T-SIG-4) is the default. A signing scheme may replace or complement it later, by a commit here.
