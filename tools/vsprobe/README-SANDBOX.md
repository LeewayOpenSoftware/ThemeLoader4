# vsprobe deliverable for Lab01 (Windows Sandbox, build 26100.9974)

Owner decision 2 of 2026-11-15: reference testing in a controlled Windows environment, non-destructive, no theme
patcher, no DLL replacement, no host change. Windows Sandbox satisfies that: the sandbox runs the host's own build and
is discarded on close; the probe only reads.

## Files

| file | what |
|---|---|
| `vsprobe.exe` | the probe, built from `tools/vsprobe/vsprobe.c` at ThemeLoader4 loader/l3b b092bc2 with `gcc.exe (MinGW-W64 x86_64-ucrt-posix-seh, built by Brecht Sanders, r8) 13.2.0`: `gcc -Wall -Wextra -O2 -municode -static vsprobe.c -luxtheme -lversion -o vsprobe.exe`; 77,436 bytes |
| `vsprobe.exe.sha256` | `2feed0e0dc23da6db8427cb7b85c9dfb3b36a92f7ae2dda8c61d6ba1e7e64b6b` |
| `queries-aero.csv` | 11,040 queries (369 classes): every VARIANT property of the stock Windows 11 26100 `aero.msstyles` as `class,part,state,property,type`, written by `tl4_loader_querylist` from the TL4 model; names and ids only |
| `vsprobe.wsb` | the sandbox configuration: networking, clipboard, printer, audio and video off, protected client, input folder read-only, output folder writable, logon command = the run script |
| `run-probe.cmd` | records the Windows version, the sha256 of the probe and of the sandbox's `aero.msstyles`, runs the probe, writes `DONE` |

## Steps for the Lab01 agent

1. Create `C:\LAB01\vsprobe\in` and `C:\LAB01\vsprobe\out` on Lab01 (or edit the two `HostFolder` paths in the `.wsb`).
2. Copy `vsprobe.exe`, `queries-aero.csv` and `run-probe.cmd` into `in`. Check `certutil -hashfile vsprobe.exe SHA256`
   against `vsprobe.exe.sha256`.
3. Double-click `vsprobe.wsb` (Windows Sandbox must be enabled on Lab01; it is a Windows feature, nothing is installed
   from outside). The logon command runs the probe; `out\DONE` appears after a few seconds.
4. Close the sandbox. Copy `out\` to `hive/shared/libernt/themeloader4/vsprobe/results-26100/` and tell god. The
   sandbox leaves nothing on Lab01.

## Output schema (`aero-26100-reference.csv`)

Two comment lines: `# vsprobe 1.0; windows <major>.<minor>.<build>; uxtheme <file version>; style bytes <n>` and the
column line. Then one row per query:

`class,part,state,property,type,hresult,origin,value`

- `class` quoted (UTF-8); `part`, `state`, `property`, `type` as asked; `hresult` as `0x%08X` (`0x00000000` = S_OK);
- `origin`: `state`, `part`, `class`, `global`, `notfound`, or `noclass` when `OpenThemeDataFromFile` returned no handle
  for the class;
- `value` by type: ENUM/INT/SIZE decimal; BOOL 0/1; COLOR `0xAABBGGRR` (COLORREF); MARGINS `l;r;t;b`; POSITION `x;y`;
  RECT `l;t;r;b`; INTLIST `count:v;v;...`; FONT `face;h=;w=;i=;u=;s=;q=` from `GetThemeFont` with a NULL DC; STRING and
  FILENAME quoted; empty on failure.

`vsprobe-stderr.txt` holds the row count and the number of classes Windows refused to open; `windows-ver.txt`,
`aero.msstyles.sha256.txt` and `vsprobe.exe.sha256.txt` document the build, the style and the binary (owner decision 2).

## What happens with the results

I compare the rows with the TL4 model's answers for the same style (same query list) and record every difference in
`docs/design/L2-MODEL-DESIGN.md` section 9 by commit: the `globals` tail of the chain, `GetThemeFilename`'s text,
RECT/MARGINS field order and BOOL normalisation are the open HYPOTHESES this run can settle.
