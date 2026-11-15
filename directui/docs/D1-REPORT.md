# D1 — bounded UIFILE parser and element model: report

Milestone D1 of ThemeLoader4, DirectUI component. Author: Stanley (Leeway Open Software), 2026-11-15.

**Labels:**
- **VERIFIED:** observed on a build or a corpus run; the method is stated.
- **SOURCE:** a code reference in this repository.
- **HYPOTHESIS:** not confirmed.

**R-DUI-18:** D1 is not used on files we did not build until the fuzz campaign report and the security review are closed.

## 1. What D1 contains

| Module | Header | Purpose | Requirements |
|---|---|---|---|
| `dui_xml` | `tl4/directui/dui_xml.h` | bounded tokenizer for the UIFILE XML subset | R-DUI-5..10, R-GEN-1..8, 9.5 |
| `dui_value` | `tl4/directui/dui_value.h` | value-expression parser with a function table | R-DUI-11..15 |
| `dui_sheet` | `tl4/directui/dui_sheet.h` | **theme entry point**: style sheets only, visual allow-list | R-DUI-1..3, R-DUI-10, R-GEN-7 |
| `dui_theme` | `tl4/directui/dui_theme.h` | abstract theme-query interface, colour resolution | D0 plan §5.5, R-DUI-15 |
| `dui_layout` | `tl4/directui/dui_layout.h` | **consumer layout entry point** (separate), generic element tree | R-DUI-4, R-DUI-16 |

- `directui/` neither includes nor links `loader/` (SOURCE: `directui/CMakeLists.txt`).
- Nothing in the core opens a file, loads a module or evaluates an expression beyond colour resolution through the caller's interface (R-GEN-1, R-DUI-5, R-DUI-13).

## 2. Design points

- **Iterative everywhere.**
  - The tokenizer keeps a fixed array of open elements (depth 32).
  - The expression parser keeps a frame stack (nesting 8).
  - Colour resolution is a bounded loop.
  - There is no recursion on input structure (R-GEN-6).
- **Two passes, then allocate** (R-GEN-7).
  - `dui_sheets_load` and `dui_layout_load` first run the tokenizer with a counting sink. That pass allocates nothing that grows with the input.
  - They then allocate exactly the counted sizes and fill from a private copy of the input with the same sink. Both passes must produce the same counts.
  - A failure returns no partial result (R-GEN-5); tests check that.
- **Spans, not pointers.** Every name and value is a `(offset, length)` span into the input (or into the private copy in a model).
- **Theme input cannot become a tree** (R-DUI-2, R-DUI-4).
  - `dui_sheets_load` accepts `duixml > stylesheets > style(resid) > rules` only. A rule is an opaque selector tag inside nested `<if>` scopes.
  - Refused: element trees, `macro`, `bind`, nested selectors and text inside a selector.
  - `dui_layout_load` is the only path to an element tree, and it is documented as consumer-only.
- **Visual allow-list** (R-DUI-3).
  - Kept: background, foreground, font (+style/size/weight/face), padding, margin, pressedpaddingshift, contentalign, height, width, minsize, borderthickness, bordercolor, borderstyle, transparent.
  - Element states on a selector (keyfocused, mousefocused, pressed, selected, mousewithin, …) are conditions. This is a HYPOTHESIS on DirectUI semantics; it is safe either way, since a condition cannot relabel or re-wire UI.
  - Every other attribute is dropped and counted under one of four reasons (content/accessibility, identity/structure, behaviour, unlisted).

## 3. Verification (VERIFIED: libernt-srv, docker compose project `tl4`, Ubuntu 24.04, gcc 13 / clang 18)

- **Builds and tests:**
  - Builds are warning-free with `-Wall -Wextra -Wconversion -Wsign-conversion -Wshadow -Werror` under gcc, under clang with ASan+UBSan, and under clang with libFuzzer.
  - All unit tests pass in all three configurations.
  - The unit tests include one negative test per rule (R-PROC-4) and limit tests at and over each 9.5 limit.
- **Fuzzing:** one harness per module, on generated seeds only (R-PROC-1): `fuzz_xml`, `fuzz_value`, `fuzz_sheet`, `fuzz_layout`. Each checks invariants: spans inside the input, tree links consistent, counts consistent, and the verdict independent of the sink. The cumulative runs per harness are in the delivery message of the last commit. There are no findings.
- **The MSVC build is not run yet.** GitHub Actions is blocked by billing; the CI workflow carries a Windows job.

### 3.1 Corpus (VERIFIED: 127 UIFILE resources from 21 reference shellstyle files, extracted outside the repository, read through `TL4_CORPUS_DIR`)

| Check | Result |
|---|---|
| Tokenizer accepts | 127 / 127 |
| Consumer layout entry builds | 127 / 127 |
| Theme entry loads the style sheets (UIFILE id 1) | 12 / 12 |
| Theme entry refuses the layout resources (`*_UIFILE`) | 115 / 115 |
| Bytes ≥ 0x80, NUL, CDATA, character references, processing instructions | 0 / 0 / 0 / 0 / 0 |

**Maxima per resource** (measured, against the limit):

| Quantity | Measured | Limit |
|---|---|---|
| size | 89,116 B | 512 KB |
| elements | 1,195 | 16,384 |
| attributes | 2,242 | 65,536 |
| depth | 15 | 32 |
| attributes per element | 11 | 64 |
| name length | 29 | 64 |
| value length | 172 B | 4,096 B |
| expression nesting | 3 | 8 |
| arguments per call | 8 | 16 |
| expression nodes per value | 20 | 256 |
| sheets | 22 | 512 |
| rules + conditions | 1,178 | 16,384 |

**Theme sheets, other figures:**
- 537 `<if>` scopes, 1,146 declarations and 4,611 value nodes at most per resource;
- 0 unknown function calls in kept values;
- 466 opaque condition names over the corpus;
- 3,933 attributes dropped over the corpus: 470 content/accessibility, 409 behaviour, 3,054 unlisted. The unlisted ones are mostly Explorer pane-sizer layout properties: `initialsize`, `reservedspace`, `framepersisttype`, `explorervisibility`, …

## 4. Findings for the requirements

1. **Stray text in the stock sheets.**
   - Every stock style sheet has a lone backslash between two rules (24 text runs over the corpus). Windows ignores it (HYPOTHESIS from it shipping).
   - D1 ignores and counts text directly inside `<style>`/`<if>`, and still refuses text inside a selector.
   - R-DUI-2 / R-DUI-7 should state this.
2. **Hex literals above 1,000,000** (34, up to `0x100000`) occur only in layout `accstate` values, never in theme input. The theme path meets R-DUI-12 as written.
3. **`sysmetric()` allow-list** (R-DUI-15): the corpus uses indices {10, 49, 50, 71, 72}, and only those are accepted.
4. **Allow-list candidates left out:** `overhang`, `themesubapp`, the Explorer sizer properties. They are layout or behaviour rather than visual. Adding one needs a commit to this report and to `dui_sheet.c`.
5. **Not measured by D1:** what DirectUI does with an unknown condition, and how rule precedence is applied. Integration (I1) must confirm these against Windows behaviour before rendering.

## 5. Running the corpus checks

Extract the UIFILE resources of the reference files into a directory outside the repository. Write `uifile.list` with one file name per line; theme style sheets have names starting with `theme`. Then run `TL4_CORPUS_DIR=<dir> ctest`.

The extraction step is replaced by the L0 bounded resource reader once `loader/` provides it. DirectUI still does not link it: the corpus tool, not the core, would use it.
