# vsprobe: Windows reference probe for visual styles

A console tool for the owner-authorized reference testing of 2026-11-15: it opens a `.msstyles` with the system's own
`uxtheme.dll` and records what the documented `GetTheme*` functions return for a list of queries, as CSV. The loader's
model (`tl4_model_find` + getters) is then compared against those rows offline.

**Non-destructive by construction.** The style is opened for this process only (`OpenThemeFile(..., global = FALSE)` and
`OpenThemeDataFromFile`, uxtheme ordinals 2 and 16, closed with ordinal 3; ordinal 4 `ApplyTheme` is never resolved);
nothing is applied, no window is themed, no registry or system
parameter is touched, no DLL is replaced. If the ordinals are missing the tool exits before any call. It needs no
elevation. Run it only in the controlled Windows environment god arranges; never on a developer host.

## Build

Windows SDK headers (`uxtheme.h`, `vssym32.h`) and one of:

```
cl /W4 /O2 vsprobe.c uxtheme.lib version.lib
clang-cl /W4 /O2 vsprobe.c uxtheme.lib version.lib
x86_64-w64-mingw32-gcc -Wall -O2 -municode vsprobe.c -luxtheme -lversion -o vsprobe.exe
```

## Run

```
vsprobe <style.msstyles> <queries.csv> <out.csv>
```

- `queries.csv`: `class,part,state,property,type` per line, UTF-8; `class` is the CMAP name (`Explorer::Button` or
  `Button`), `type` the TMT primitive (200..211) the style's record carries. `tl4_loader_querylist` (loader/tools)
  writes this list from a style's model, so the Windows side asks exactly the (class, part, state, property) tuples the
  file defines; the list holds names and ids only.
- `out.csv`: a header with the Windows build, the `uxtheme.dll` version and the style's byte size, then one row per
  query: `class,part,state,property,type,hresult,origin,value`. Values: ints as decimal, colours as `0xAABBGGRR`
  COLORREF, margins `l;r;t;b`, positions `x;y`, rects `l;t;r;b`, int lists `count:v;v;...`, fonts
  `face;h=;w=;i=;u=;s=;q=` (`GetThemeFont` with a NULL DC), strings and file names quoted.

## What to document with the results (owner decision 2)

The Windows edition and build (the header row), the `uxtheme.dll` version, the exact style file (size in the header;
record its sha256 next to it), the query list's commit, and every difference against the loader's model: a difference
is a HYPOTHESIS about Windows behaviour turned into a measurement, and goes into `docs/design/L2-MODEL-DESIGN.md`
section 9 by commit.
