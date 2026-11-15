# Theme Loader 4

Private Leeway Open Software project. It is an independent, testable loader for **version-4 `.msstyles` visual styles** (Windows 10/11, `PACKTHEM_VERSION` 4). Next to it, but separate, is a **DirectUI** component: `dui70` functionality, UIFILE parsing, and the contracts Explorer needs.

The target is a later integration into **LiberX** and Orosi. That happens only when the interfaces, security boundaries and behaviour are mature.

## Scope (owner order, 2026-11-15)

- Independent of the LiberNT kernel and the desktop integration. This is **not** a rewrite of UxTheme, Explorer or DWM.
- The foundation is Ryan's format analysis (`docs/design/`). Jim 2's security review (`docs/security/REQUIREMENTS.md`) is a set of **mandatory** constraints, accepted before the binary reader is written.
- What this means for the code:
  - bounded resource parsing (our own bounded PE/resource reader, not the system walker);
  - strict reference validation;
  - cycle detection;
  - memory limits;
  - one validator shared by install and load.
- Start minimal: a v4 container validator and a property model. Full visual compatibility is not a first-step goal.
- The theme loader must **never** depend on DirectUI. DirectUI develops in parallel.
- The existing v3 engine stays compatible. Nothing here changes the v3 path until an integration milestone is approved.
- **No Microsoft or third-party theme assets in this repository**: no `.msstyles`, `.mui`, `shellstyle.dll`, wallpapers or extracted images. Compatibility tests read a local reference corpus given by path (`tests/corpus/README.md`). Test fixtures are files we generate ourselves.

## Layout

| Path | Contents |
|---|---|
| `loader/` | Theme Loader 4: bounded PE/resource reader, v4 validator, property model, query API (C11, host-portable, no Windows dependency in the core) |
| `directui/` | DirectUI: UIFILE parser and element model; independent of `loader/` |
| `tests/` | unit tests, generated fixtures, fuzz harnesses, corpus runner |
| `docs/design/` | format and loader design |
| `docs/security/` | mandatory security requirements |

## Milestones

Implementation, integration and security testing are separate milestones.

**Loader**

- **L0**: scaffolding (CMake, CI on Linux and Windows hosts), the bounded PE/resource reader, and a libFuzzer harness from the first commit.
- **L1**: v4 container validator (CMAP/VMAP/RMAP/VARIANT walk, limits, reference checks, cycle checks). It validates only and allocates nothing before it passes. Corpus runner: the 32 reference styles validate, and their statistics match Ryan's measurements.
- **L2**: property model with binary values, plus a query API mirroring the `GetTheme*` subset. A v3 adapter keeps the current engine's behaviour.
- **L3**: string and image resolution: a bounded string-table reader (no MUI redirection) and a PNG chunk walker (IHDR/PLTE/tRNS/IDAT/IEND only) before any decoder.

**Security testing**

- **S1**: fuzzing campaign on L0–L1 plus a Jim 2 review.
- **S2**: the same for L2–L3, including the image and string paths.

**DirectUI**

- **D0**: research: the `dui70` exports and behaviour that Explorer and shell consumers need, and the UIFILE format observed in the reference `shellstyle.dll` files (read locally, never committed).
- **D1**: bounded UIFILE parser and element model, with standalone tests.

**Integration** (owner-gated)

- **I1**: integration design for LiberX/Orosi uxtheme, guided by the pipeline inventory and the VM findings.

## Rules

- Author and committer: `LeewayOpenSoftware <281492504+LeewayOpenSoftware@users.noreply.github.com>`. No AI co-author trailers.
- Every claim in the docs is labelled VERIFIED, SOURCE (path:line) or HYPOTHESIS.
- License: LGPL-2.1-or-later (`COPYING.LIB`). This keeps integration possible with the Wine/ReactOS-lineage uxtheme.
