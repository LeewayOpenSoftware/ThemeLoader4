# D0 — DirectUI research

Milestone D0 of ThemeLoader4 (research only). Author: Stanley (Leeway Open Software), 2026-11-15.

**Evidence labels:**
- **VERIFIED:** observed on the reference files or a reference Windows host, with the method stated.
- **SOURCE:** a code reference (`path:line`) in LiberNT public `leeway/main` 177706459df.
- **HYPOTHESIS:** not confirmed.

**Inputs:**
- Reference `shellstyle.dll` / `shellstyle.dll.mui` files: the owner's research copies, read locally with a statistics-only survey script.
- `dui70.dll` and its importers on a Windows 11 host: export names and import tables only.

**What this document does not contain:** none of those files and none of their contents. It records counts, structure and grammar shapes only (digits and identifiers normalised). This follows the repository rule against Microsoft or third-party assets.

---

## 1. What DirectUI is, and who consumes it

DirectUI (`dui70.dll`) is the retained-mode UI framework behind the Explorer frame and parts of the shell and Control Panel. It provides:
- an element tree (`DirectUI::Element` and subclasses) with properties, layouts and events;
- a style-sheet engine (selectors with state conditions);
- an XML parser (`DUIXmlParser`) that builds element trees and style sheets from **UIFILE** resources.

Visual-style data reaches DirectUI in two ways:
1. through the uxtheme API, from inside UIFILE value expressions (§3.4);
2. through `shellstyle.dll`, which carries the shell's **style sheets** and bitmaps.

### 1.1 Export surface of the reference dui70.dll (VERIFIED: export table of a Windows 11 host's System32\dui70.dll)

Host `dui70.dll` 10.0.26100.9549 has **4,321 named exports and no ordinal-only exports**.

**Most of the surface is the C++ ABI** (MSVC-decorated `DirectUI::` members) of 180 classes. The largest, by number of exported members:

| Class | Members | Class | Members |
|---|---|---|---|
| `Element` | 416 | `Schema` | 171 |
| `DUIXmlParser` | 147 | `Value` | 108 |
| `TouchScrollViewer` | 88 | `HWNDHost` | 73 |
| `TouchEditBase` | 72 | `HWNDElement` | 61 |
| `TouchSlider` | 58 | `TouchHWNDElement` | 54 |
| `Movie` | 53 | `RichText` | 51 |
| `EventManager` | 45 | `DuiAccessible` | 44 |
| `ScrollBar`, `Edit`, `DialogElement`, `ModernProgressBar`, `TaskPage`, `Viewer`, `Macro`, `Button`, `Combobox`, `Layout`, `Progress`, `Selector`, `Expando`, `Navigator`, `Bind`, `Pages`, `Repeater` | 12–39 each | Layouts: `BorderLayout`, `GridLayout`, `NineGridLayout`, `FlowLayout`, `VerticalFlowLayout` | 9–10 each |

**Families:**
- the element core: `Element`, `Value`, `Schema`, `ClassInfoBase`, `EventManager`;
- the XML/UIFILE parser: `DUIXmlParser`;
- layouts;
- HWND hosting: `HWNDElement`, `HWNDHost`, `NativeHWNDHost`;
- common-control wrappers: `CC*`;
- touch controls: `Touch*`;
- UI Automation and MSAA providers: `*Provider`, `DuiAccessible`;
- animation: `PVLAnimation`, `AnimationStrip`, `Movie`.

**A flat C surface (about 80 names) also exists** and is the more stable target:
- lifecycle: `InitProcessPriv`, `InitThread`, `UnInitProcessPriv`, `UnInitThread`, `SkipDLLUnloadInitChecks`;
- control registration: `RegisterAllControls`, `RegisterBaseControls`, `RegisterCommonControls`, `RegisterStandardControls`, `RegisterExtendedControls`, `RegisterMacroControls`, `RegisterMiscControls`, `RegisterBrowserControls`, `RegisterXControls`, `RegisterPVLBehaviorFactory`;
- a C wrapper of the parser and element: `DUI70_DUIXmlParserCreate`, `DUI70_DUIXmlParserSetXMLFromResource`, `DUI70_DUIXmlParserCreateElement`, `DUI70_DUIXmlParserDestroy`, `DUI70_Element*` (find, children, root, listeners, defer, properties), `DUI70_ValueRelease`, `DUI70_RichText*`;
- theme and graphics helpers: `GetThemeHandle`, `FlushThemeHandles`, `PurgeThemeHandles`, `DrawShadowTextEx`, `DUIDrawShadowText`, `BlurBitmap`, `ProcessAlphaBitmapI`, `ColorFromEnumI`, `ARGBColorFromEnumI`, `BrushFromEnumI`;
- DPI: `GetScaleFactor`, `GetDesktopDPI`, `SetDesktopDPIOverride`, `AllowAllScalingPlateaus`;
- `StrToID`, `CreateDUIWrapper*`, `DuiCreateObject`, `StartMessagePump`, `StopMessagePump`, animation enable/disable, `PreprocessBuffer`, `InitPreprocessor`.

**Observations:**
- Our stub exports exactly the five lifecycle names from this list (§1.3).
- `GetThemeHandle`/`FlushThemeHandles` show that dui70 keeps its own cache of uxtheme handles (HYPOTHESIS on the exact behaviour).
- `PreprocessBuffer`/`InitPreprocessor` suggest a UIFILE preprocessing step before XML parsing (HYPOTHESIS; macros or conditional sections). None is visible in the corpus text, which is plain XML.

### 1.2 Consumers (VERIFIED: import tables of the same host's System32)

**107 System32 binaries import dui70** (the number of imported functions is in parentheses).

| Area | Importers |
|---|---|
| **Explorer and shell** | `ExplorerFrame.dll` (529), `shell32.dll` (469), `twinui.dll` (328), `Windows.UI.Immersive.dll` (344), `twinui.pcshell.dll`, `stobject.dll`, `autoplay.dll`, `fontext.dll`, `efsext.dll`, `comdlg32.dll` (5) |
| **Control Panel** | **`themecpl.dll` (241, the Personalization page)**, `usercpl.dll`, `systemcpl.dll`, `appwiz.cpl`, `timedate.cpl`, `powercpl.dll`, `taskbarcpl.dll`, `accessibilitycpl.dll`, `ActionCenterCPL.dll`, `DiagCpl.dll`, `FirewallControlPanel.dll`, `netcenter.dll`, `hgcpl.dll`, `fhcpl.dll`, `fvecpl.dll`, `sdcpl.dll`, `sharemediacpl.dll`, `werconcpl.dll`, `UserAccountControlSettings.dll`, `racpldlg.dll`, `WorkfoldersControl.dll`, `Vault.dll`, `wdc.dll` |
| **Logon, lock and boot** | `authui.dll`, `bootux.dll`, `LockHostingFramework.dll`, `LockScreenContent*.{dll,exe}`, `Windows.UI.PicturePassword.dll`, `sessionmsg.exe` |
| **Tools and accessibility** | `Taskmgr.exe` (378), `mmc.exe`, `Narrator.exe`, `osk.exe`, `sethc.exe`, `EaseOfAccessDialog.exe`, `msdt.exe`, `msctfuimanager.dll` (IME UI) |
| **Other UI** | `aclui.dll` (security tab), `CertEnrollUI.dll`, `DevicePairing.dll`, `WebcamUi.dll`, `InputSwitch.dll`, `wlanui.dll`, … |

**Consequences:**
1. DirectUI on Windows is a **shared UI framework for many system components**, not an Explorer detail.
2. The Windows Personalization page (`themecpl.dll`) is itself DirectUI-based. This is relevant to the owner's Personalization CP redesign, which LiberNT is building as classic Win32 inside the Explorer namespace. Those are two different implementations of the same page, and only the visible structure has to match.
3. **No LiberNT binary needs dui70 today** (§1.3). A dui70 ABI only becomes necessary if Windows binaries that import it are to run on LiberNT. That is an integration decision (I1), not a D1 goal.

### 1.3 LiberNT today

- **`dll/win32/dui70` is a stub.** Five lifecycle entry points (`InitProcessPriv`, `InitThread`, `SkipDLLUnloadInitChecks`, `UnInitProcessPriv`, `UnInitThread`) plus `DllMain`. No element, parser or layout code. (SOURCE: `dll/win32/dui70/dui70.spec:1-8`, `dui70_main.c:17-72`; GPL-3.0-or-later header, `dui70_main.c:1-5`, which matters for reuse under this repository's LGPL.)
- **Nothing parses UIFILE.** The only shellstyle hook is `shell32!SHGetShellStyleHInstance`, which loads the module (SOURCE: `dll/win32/shell32/utils.cpp:505-516`). `git grep -i 'uifile|duixml|DirectUI'` finds nothing outside the dui70 stub.
- **Explorer, the shell and Control Panel are not DirectUI-based.** The Explorer frame, navigation bar and Control Panel are classic Win32/ATL code that uses uxtheme classes directly (see `research-dwm/02-OROSI-PIPELINE-INVENTORY.md` §4).

## 2. Resource layout of the reference shellstyle files (VERIFIED: survey of 21 files: 10 shellstyle.dll, 1 shellstyle_original.dll, 10 .mui)

**Stock Windows 10/11 `shellstyle.dll`** (x64, a resource-only PE: 0 exports, 0 imports):
- exactly **one `UIFILE` (id 1)**: the shell style sheets;
- 14 `IMAGE` and 16 `RT_BITMAP` resources;
- `MUI`, `RT_VERSION` and `RT_HTML`.

**Stock `.mui`:**
- 1 `RT_STRING` block;
- 22 `RT_RCDATA`. All 220 RCDATA resources across the corpus are **PNG** (signature `89504e470d0a1a0a`), so these are images, not strings;
- 2 `RT_HTML`;
- `MUI` and `RT_VERSION`.

**Third-party styles add more UIFILE types:**
- They carry their own `UIFILE` (style sheets) **plus whole replacement layouts** under the custom resource types `SHELL32_UIFILE` (3–17 per file), `EXPLORERFRAME_UIFILE` (1–2), `SYSTEMCPL_UIFILE` (1) and `USERCPL_UIFILE` (23, in `shellstyle_original.dll`).
- **HYPOTHESIS** (consistent with the order's README analysis): these are consumed by third-party UIFILE-override hooks (Windhawk/OldNewExplorer-style), which substitute them for the layout resources compiled into shell32.dll, explorerframe.dll and the control panels.
- They are **not part of the native shellstyle contract**: stock Windows only reads `UIFILE` id 1 from shellstyle.dll. That is the native contract to target.
- The older Win10-1507/1511 x86 builds use x86 PEs and keep only `UIFILE` + bitmaps.

| Resource type | Files | Count/file | Content |
|---|---|---|---|
| UIFILE | all 11 DLLs | 1 (2 in one) | `<duixml><stylesheets>…` style sheets |
| SHELL32_UIFILE | 6 third-party | 3–17 | layouts (`<duixml>` with `<Element>` trees; 4 use the root `<hwndview>`) |
| EXPLORERFRAME_UIFILE | 4 third-party | 1–2 | ribbon/UI-collection layouts (`UICollection`, `UIItem`, `UIRootItem`, …) |
| SYSTEMCPL_UIFILE / USERCPL_UIFILE | 6 / 1 | 1 / 23 | Control Panel page layouts (`SystemCPLPage`, `UserCplPage`) |
| IMAGE / RT_BITMAP | all | 14 / 16–45 | images used through `graphic()` / `icon()` references |
| RT_RCDATA (.mui) | all .mui | 22 | PNG images |
| RT_STRING (.mui) | all .mui | 1 block | strings for `resstr()` |

## 3. The UIFILE dialect (VERIFIED: 127 UIFILE resources, 571–89,116 bytes, all parsed by a strict XML parser)

### 3.1 Encoding and XML features

- Encoding: UTF-8/ANSI with **no BOM and no `<?xml?>` declaration** (127/127).
- No DOCTYPE and no entity references (0/127).
- 52 comments in total.
- Element nesting depth is at most 14.
- Every root is `<duixml>`, except 4 layouts with the root `<hwndview>`.
- Tag and attribute names are **case-insensitive in practice**: `Element`/`element`, `Button`/`button` and `KeyFocused`/`keyfocused` all occur. A parser must fold case for tags and attributes, but not for values (HYPOTHESIS on the original's exact rules).
- 129 distinct tags and 198 distinct attribute names.

### 3.2 Style sheets

```
<duixml>
  <stylesheets>
    <style resid="NAME">            415 sheets, every one with resid
      <SelectorTag attr=value …>    2,152 selectors (tag + optional id/class match)
        <if cond=value …> props </if>  2,471 nested conditions
      </SelectorTag>
      <if cond=value …> … </if>     6,613 top-level conditions
    </style>
  </stylesheets>
</duixml>
```

- **Condition attributes** (top 10 of 30+): `id`, `class`, `mousefocused`, `keyfocused`, `selected`, `pressed`, `enabled`, `propertyindex`, `SlidesUpAndDown` and `Vertical`, plus state flags such as `WindowActive`, `mousewithin`, `tabletmode` and `hoveredover`.
- A **layout references a sheet** by name with `sheet="NAME"` (651 uses; always a plain resid name).

### 3.3 Layouts (element trees)

- **Most-used tags:** `element`/`Element` (6,280), `Sizer` (1,698), `Button` (1,382+369), `Button3d`, `TemplateBackground`, `MetadataElement`, `MetadataLabel`, `ControlPanelLink`, `UISmartProperty`, `bind` (310), `InplaceEdit`, `PreviewThumbnail`, `NavigateButton`, `UIItem`, `ViewHost`, `CheckboxButtonElement`, `macro` (62), `ModernProgressBar`, and the `CC*` wrappers around common controls (`CCPushButton`, `CCRadioButton`, `CCSysLink`, `CCCheckBox`).
- **Most-used attributes:** `id` (7,516), `background`, `contentalign`, `foreground`, `layoutpos`, `class`, `padding`, `layout`, `font`, `accessible`/`accRole`/`accname`/`accdefaction`, `height`/`width`, `content`, `resid`, `cursor`, `sheet`, `borderthickness`, `margin`, `MinSize`, `tooltip`, `shortcut`.
- **Element types are open-ended:** many tags are classes registered by the consuming binary (shell32, explorerframe, the control panels), not by dui70 (HYPOTHESIS, consistent with the export surface in §1.1). A parser must therefore accept **unknown tags as generic elements** and report them; it must not fail on them.

### 3.4 Value expressions

Values are either:
- plain: an identifier (20,948), a number (3,472), a hex number (34), or other text (6,159, e.g. `rp` units);
- a function-call expression, possibly nested.

Observed **function signatures** (argument kinds, the counts of the most frequent forms):

| Function | Forms | Meaning (HYPOTHESIS, from names) |
|---|---|---|
| `atom(ident)` | 8,007 | interned identifier (ids, classes) |
| `rect(a,b,c,d)` | 3,843; args are numbers or `Nrp` | rectangle; `rp` = relative (DPI-scaled) pixels |
| `gtc(class,part,state,prop)` | 3,688 | **GetThemeColor** |
| `dtb(class,part,state)` | 3,191 | **DrawThemeBackground** as a fill |
| `gtf(class,part,state)` | 2,249 | **GetThemeFont** |
| `gtmar(…)` / `gtmet(…)` | 25 / 11 | GetThemeMargins / GetThemeMetric |
| `themeable(themed, fallback)` | 5,001 | themed value, with a classic fallback (system colour name, `argb()`, `dfc()`) when no theme is active |
| `argb(a,r,g,b)` / `rgb(r,g,b)` | 1,892 / 3 | colours |
| `dfc(…)` | 166 | DrawFrameControl fallback |
| `sysmetric(n)` | 354 | GetSystemMetrics |
| `resstr(id[, library(dll)])` | 1,078 (380 with a library) | string resource, optionally from **another module** |
| `icon(id, cx, cy, library(dll))` / `graphic(id, …, library(dll))` | 158 / 86 | image resources, optionally from another module |
| `library(name)` | 600 | names a module, e.g. `shell32.dll` |
| `size(a,b)`, `gradient(c1,c2,c3,mode)` | 233 / 57 | |
| Layouts: `borderlayout()` 1,170, `flowlayout(…)` 669, `filllayout()` 453, `verticalflowlayout(…)` 156, `rowlayout(n)` 114, `gridlayout(r,c)` 7, `ninegridlayout`, `overflowlayout`, `centeringlayout`, `shellborderlayout`, `viewareaawarelayout`, `featureenabledlayout`, … | | built-in plus consumer-registered layouts |

**Theme classes referenced from UIFILE** (shape survey): `CONTROLPANELSTYLE`, `TEXTSTYLE`/`TextStyle`, `CommandModule`, `CPLCommandModule::CommandModule`, `PreviewPane`, `ReadingPane`, `CONTROLPANEL`, and others.

**Consequence: DirectUI depends on the uxtheme *API*, not on the theme loader.** DirectUI calls GetThemeColor, GetThemeFont and DrawThemeBackground like any other uxtheme client. Loader independence (owner rule) holds if DirectUI is written against an abstract theme-query interface (§5).

## 4. Security notes for a UIFILE parser (feeds Jim 2's review; requirements A1–A3 and A11–A12 apply by analogy)

1. **Cross-module references.** `library(dll)` in `resstr` / `icon` / `graphic` makes a style file name other modules. A loader must map them **as data only, from the same trusted store** (requirement A1/A3 analogue), resolve ids within checked resource bounds, and refuse paths (only bare module names from an allow-list).
2. **Replacement layouts are code-like.** `*_UIFILE` layout overrides change the structure, bindings and command elements (`bind`, `macro`, `ControlPanelLink`, `NavigateButton`, `shellitemlink`) of trusted UI. **Recommendation:** a theme must never be allowed to replace layouts. Only style sheets (`UIFILE` id 1) are theme data; layouts belong to the consuming binary. This mirrors stock Windows behaviour (VERIFIED: stock shellstyle carries only UIFILE id 1).
3. **Parser limits:**
   - XML only, with no DTD and no entities (none occur; reject them);
   - maximum resource size (corpus maximum 89 KB; suggest 512 KB);
   - maximum depth (corpus 14; suggest 32);
   - maximum attributes per element;
   - maximum expression nesting depth and argument count;
   - numbers range-checked;
   - `rp` values clamped.
4. **`macro` / `bind`** imply in-file reuse and data binding. Expansion must be bounded (no recursion and a total node budget) so a small file cannot expand exponentially.
5. **Case folding** of tag and attribute names must be ordinal ASCII, so that no locale-dependent comparison is involved.

## 5. D1 plan — a bounded UIFILE parser and element model (independent of `loader/`)

**Scope:** parse style sheets (native contract) and, for testing only, layouts, into an in-memory model. **No rendering and no Win32 dependency in the core.** C11, host-portable, the same build and fuzz setup as `loader/`. **No link or include dependency on `loader/`.**

1. **`dui_xml`:** a minimal, bounded XML tokenizer for the observed subset.
   - Supported: elements, attributes, comments, UTF-8/ANSI.
   - Rejected: DTD, entities other than the 5 predefined ones, CDATA (unless the corpus needs it), processing instructions.
   - Limits: size, depth, attributes per element, name length.
   - The input is a `(pointer, size)` resource; nothing is NUL-terminated.
2. **`dui_value`:** an expression parser for the §3.4 grammar.
   - Typed AST: number, hex, `rp` length, identifier, string, call(name, args).
   - Bounded nesting and argument count.
   - Known functions are checked for arity and argument kind; unknown functions are kept as generic calls and counted.
3. **`dui_sheet`:** a style-sheet model: `style(resid)` → rules (selector tag + attribute matches) → conditions (`if` attribute predicates) → property assignments. Selector and condition matching against an element's state bitset, with deterministic precedence (document order; to be confirmed against Windows with HYPOTHESIS tests).
4. **`dui_element`:** a generic element model.
   - Tag, id, class, property bag, children.
   - A registry for known element types and layouts, with unknown tags accepted as generic elements.
   - **Layout overrides are not loaded from theme files** (§4.2).
5. **The theme-query interface:** a small vtable (`get_color`, `get_font`, `get_margins`, `get_metric`, `draw_background` as a deferred fill descriptor) that the integration (I1) binds to uxtheme. The tests bind it to a fake. This keeps DirectUI free of any dependency on the loader.
6. **Tests:**
   - unit tests per module;
   - **generated fixtures** written by us, which imitate the grammar shapes in §3 with no copied content;
   - a corpus runner over `TL4_CORPUS_DIR` that reports statistics matching §2–§3 (127 UIFILEs, 0 parse errors, depth ≤ 14, the tag, attribute and function counts);
   - a libFuzzer harness for `dui_xml` and `dui_value` from the first commit.
7. **Not in D1:** layout computation, rendering, events, accessibility, and any dui70.dll ABI. The exported C++ ABI in §1.1 is not a goal until integration shows a consumer that needs it; LiberNT has none today (§1.3).

**Exit criteria for D1:**
- the corpus runner parses all reference style sheets with 0 errors;
- the fuzzers have run for an agreed time with no findings;
- Jim 2 has reviewed §4;
- `directui/` builds and tests without `loader/`.
