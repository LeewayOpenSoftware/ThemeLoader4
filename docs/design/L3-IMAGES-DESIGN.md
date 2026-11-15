# L3 design note: the PNG chunk walker

Ryan, 2026-11-15; Jim 2 ruled on section 5 the same day (REQUIREMENTS on main 34501f7) and set five MUSTs, all
folded in below and marked (MUST n). Labels as everywhere in `docs/`: VERIFIED (measured on the
32 reference styles, read-only, or in the lab), SOURCE (`path:line`; LiberNT tree cand3 6f817ad0bf7 for the engine,
this repository for the loader), HYPOTHESIS. `docs/security/REQUIREMENTS.md` is normative; section 5 of this note
proposes amendments and Jim 2 decides them.

## 1. What L3 is and is not

The README names L3 as "string and image resolution: a bounded string-table reader (no MUI redirection) and a PNG
chunk walker (IHDR/PLTE/tRNS/IDAT/IEND only) before any decoder." The string-table reader exists since L1
(`tl4_v4_find_string`, R-RES-1) and L2 resolves every string reference at build. What remains is the **walker**:

- Input: the `(offset, size)` of an `IMAGE` or `STREAM` resource that `tl4_get_filename` / `tl4_get_diskstream`
  return (L2), inside the style mapping; or any `(data, size)` the caller holds (the core reads nothing else, R-GEN-1).
- Checks (R-RES-3, R-RES-4, R-RES-5): the PNG signature, the chunk structure with every length inside the resource and
  every CRC, an allow-list of chunk types each with its exact or bounded length, IHDR first with the accepted parameter
  set, the limits of section 6 taken from IHDR, IDAT accounting, IEND last and the resource ending exactly there.
- Output: counts and the header values (`tl4_png_info`), and on request the IDAT ranges in order, for a consumer that
  feeds a decoder.
- Not L3: decoding, zlib, colour management, the per-pixel budget's owner (the consumer holds the budget instance, the
  core provides the arithmetic), bitmaps (R-RES-5: PNG only; DirectUI's DIB check is R-DUI-14's own item).

The decoder being fenced: the engine hands the resource bytes to GDI+ (`SHCreateMemStream` + `Gdiplus::Bitmap::FromStream`,
SOURCE pngsup.cpp:25-28), which in LiberNT is Wine's gdiplus over windowscodecs, whose PNG codec is `pngformat.c` on
libpng and zlib (SOURCE dll/win32/windowscodecs/CMakeLists.txt:11-12,35,41,92). None of that is touched by L3; the
walker decides what reaches it.

## 2. What the corpus says (VERIFIED, 2026-11-15, read-only over the 32 styles: 30,171 IMAGE and STREAM resources)

| fact | value |
|---|---|
| format | 30,171 of 30,171 start with the PNG signature; 0 BMP, 0 other (R-RES-5 holds) |
| chunk types seen | IHDR 30,171 · gAMA 30,171 · pHYs 30,171 · tEXt 27,976 · sRGB 2,300 · IDAT 30,297 · IEND 30,171; **no PLTE, no tRNS, nothing else** |
| chunk orders | exactly three: `IHDR gAMA pHYs tEXt IDAT… IEND` (27,871), `IHDR sRGB gAMA pHYs IDAT… IEND` (2,195), `IHDR sRGB gAMA pHYs tEXt IDAT… IEND` (105) |
| ancillary lengths | gAMA 4, pHYs 9, sRGB 1, tEXt 25 or 27 bytes (keyword `Title` in all 27,976, printable Latin-1 only); none repeated in an image; none after the first IDAT |
| IHDR | bit depth 8; colour type 6 (26,729) or 2 (3,442); compression 0; filter 0; interlace 0: exactly R-RES-3's accepted set |
| IDAT | one chunk in 30,081 images; 2..6 chunks in 90 (24 IMAGE with 2; STREAMs with 2..6), always contiguous; largest chunk 65,524 bytes; largest total 85,201 (IMAGE), 345,332 (STREAM) |
| structure | 0 length or order problems, 0 CRC mismatches, IEND last in all, 0 trailing bytes after IEND; at most 12 chunks per image |
| dimensions | largest side 3,720; largest image 1,237,800 pixels; most pixels per style 6,476,678 (stock aero); largest resource 345,499 bytes |

Consequence for R-RES-3 as written ("no chunk type other than IHDR, PLTE, tRNS, IDAT, IEND"): **every reference image
is rejected**, because every one carries gAMA and pHYs and most carry tEXt. R-RES-3 foresaw this ("widened only with
corpus evidence"); section 5 proposes the widening with the exact lengths measured.

## 3. The walker

### 3.1 API (loader/include/tl4/loader/png.h, C11, no allocation, no OS call)

```
typedef struct tl4_png_info {
    uint32_t width, height;            /* 1..TL4_PNG_MAX_SIDE */
    uint8_t  bit_depth, colour_type;   /* 8; 2 or 6 */
    uint8_t  interlace;                /* 0 */
    uint8_t  has_srgb, has_gama, has_phys, has_text;   /* the allowed ancillary chunks, bounded and range-checked */
    uint32_t chunks, idat_chunks;      /* counts */
    uint32_t idat_bytes;               /* the compressed stream, <= TL4_PNG_MAX_COMPRESSED */
    uint64_t pixels;                   /* width * height, <= TL4_PNG_MAX_PIXELS */
} tl4_png_info;

/* The whole check (R-RES-3/4/5). On failure *info is cleared (R-GEN-5). */
tl4_status tl4_png_check(const uint8_t *data, uint32_t size, tl4_png_info *info);

/* The IDAT chunk data ranges in file order, as (offset, size) into `data`, for a consumer that concatenates them for
 * a decoder: tl4_png_check's own single pass with a callback, every check and every CRC again (MUST 2, R-GEN-11). */
typedef int (*tl4_png_idat_cb)(uint32_t offset, uint32_t size, void *context);
tl4_status tl4_png_idat_enum(const uint8_t *data, uint32_t size, tl4_png_idat_cb cb, void *context);

/* R-RES-4: decoded pixels per style, per process. The consumer owns the instance; the core does the checked sum. */
typedef struct tl4_pixel_budget { uint64_t limit, used; } tl4_pixel_budget;
tl4_status tl4_pixel_budget_take(tl4_pixel_budget *b, uint64_t pixels);   /* TL4_E_LIMIT when it would exceed */
```

New status codes: `TL4_E_PNG_SIGNATURE`, `TL4_E_PNG_CHUNK` (length, order, repeat, unknown type), `TL4_E_PNG_CRC`,
`TL4_E_PNG_IHDR` (parameter set), `TL4_E_LIMIT` (a section 6 image limit).

### 3.2 The walk (R-GEN-2/3/4/5/6/11)

1. `size >= 8 + (12 + 13) + 12 + 12` (signature, IHDR, one IDAT, IEND) and `size <= TL4_PNG_MAX_RESOURCE`
   (4 MB + headroom for the chunk overhead; the compressed stream itself is bounded separately), else reject.
2. The 8 signature bytes exactly.
3. Chunks, in one loop bounded by `TL4_PNG_MAX_CHUNKS` (section 4): read `length` (big-endian, read once into a local)
   and `type`; `length < 2^31` (PNG) and `12 + length <= remaining` in 64-bit arithmetic; the CRC-32 of type + data is
   computed with a static 256-entry table and compared with the stored one (every chunk, R-RES-3); then the type:
   - `IHDR`: only as the first chunk, length 13; width and height 1..`TL4_PNG_MAX_SIDE`, `width * height <=
     TL4_PNG_MAX_PIXELS` in 64 bits (R-RES-4, before any decoder), bit depth 8, colour type 2 or 6, compression 0,
     filter 0, interlace 0 (R-RES-3); anything else is a reject, not a downgrade.
   - the ancillary allow-list, one table (MUST 1, as ruled): `gAMA` (length 4, gamma value ≠ 0), `pHYs` (9, unit byte
     0 or 1), `sRGB` (1, rendering intent 0..3), `tEXt` (2..`TL4_PNG_MAX_TEXT`, a keyword of 1..79 printable Latin-1
     bytes, exactly one NUL, printable Latin-1 text: 32..126 or 161..255): each at most once, only before the first
     IDAT, **not interpreted** (the values are range-checked and never read into a decision).
   - `PLTE` and `tRNS`: rejected in every shape (no corpus evidence; ruled).
   - `IDAT`: one or more, **contiguous** (an IDAT after a non-IDAT that followed an IDAT is a reject), each counted in
     `idat_chunks <= TL4_PNG_MAX_IDAT_CHUNKS`, `idat_bytes` summed in 64 bits and `<= TL4_PNG_MAX_COMPRESSED`.
   - `IEND`: length 0, only after at least one IDAT, and the walk must then be exactly at `size` (no trailing bytes).
   - any other type, critical or ancillary (`iCCP`, `cHRM`, `bKGD`, `tIME`, `zTXt`, `iTXt`, `sBIT`, `hIST`, `sPLT`,
     `eXIf`, private types): a reject (R-GEN-5: never skipped).
4. Done only when IEND was seen; a resource that ends before IEND is a reject.

`tl4_png_idat_enum` is this same pass with the callback set (MUST 2): no second parser, the CRCs are computed every
time. The CRC table is a `static const` array in the source (MUST 3), not built at first use.

Everything the walker needs is read once into locals; the decoder later reads the bytes again, so the consumer rule of
section 5 (R-RES-6 proposal) closes that window: the consumer copies the resource once into its own buffer, runs the
walker **on the copy**, and decodes **the copy**. `SHCreateMemStream` already copies (pngsup.cpp:25); the order (copy,
check, decode) is what changes.

### 3.3 Cost

One pass over the resource: O(size) for the CRC, nothing else. At the limits (4 MB) a few milliseconds; the R-V4-14
style of tests gets an image at the limits (8,192 × 1,024 with 4 MB of IDAT in 58 chunks: 64 chunks in all with the
ancillary ones) and the same CPU bound.

## 4. Limits (section 6 of REQUIREMENTS, with the proposals of this note)

| quantity | corpus maximum | normative (section 6) | proposed here (new) |
|---|---|---|---|
| side | 3,720 | 8,192 | — |
| pixels per image | 1,237,800 | 8 M | — |
| compressed bytes per image (IDAT total) | 345,332 | 4 MB | — |
| decoded pixels per style per process | 6,476,678 | 16 M | — |
| resource bytes | 345,499 | — | 4 MB + 64 KB (`TL4_PNG_MAX_RESOURCE`: the stream plus bounded chunk overhead) |
| chunks per image | 12 | — | 64 (`TL4_PNG_MAX_CHUNKS`) |
| IDAT chunks per image | 6 | — | 64 (`TL4_PNG_MAX_IDAT_CHUNKS`) |
| ancillary chunks | gAMA 4, pHYs 9, sRGB 1, tEXt 27 | — | each once; gAMA 4, pHYs 9, sRGB 1 exact; tEXt ≤ 256 (`TL4_PNG_MAX_TEXT`) |
| IMAGE / STREAM count | 1,611 / 3 | 4,096 / 16 | checked by L1 already |

## 5. Requirement changes (ruled by Jim 2, 2026-11-15; normative on main 34501f7)

The proposals as submitted, with the ruling after each:

- **R-RES-3 (amend, corpus evidence of section 2)**: the allowed chunk types are IHDR, PLTE, tRNS, IDAT, IEND **and the
  ancillary gAMA (4 bytes), pHYs (9), sRGB (1) and tEXt (bounded, printable, a NUL in the first 80 bytes), each at most
  once and before the first IDAT, never interpreted**. Rationale: all 30,171 reference images carry gAMA and pHYs,
  27,976 carry tEXt, 2,300 sRGB; the rule as written rejects the whole corpus. Alternative considered and not
  recommended: hand the decoder a rebuilt stream without the ancillary chunks. That is a repaired input (R-GEN-5) and
  gAMA/sRGB can change what libpng renders (HYPOTHESIS: GDI+ ignores them; not measured). **Ruled**: accepted with
  value ranges (gAMA ≠ 0, pHYs unit 0 or 1, sRGB 0..3, keyword 1..79).
- **R-RES-6 (new)**: a resource reaches a decoder only as a private copy that the walker checked after the copy was
  taken; the walker's result is never applied to bytes it did not read (R-GEN-11 across the walk/decode window).
  **Ruled**: accepted with two additions: the size limit applies before the copy, and after decoding the consumer
  compares the decoded width and height with the walker's. This is a consumer change in `pngsup.cpp` and belongs to
  the integration milestone I1 only; nothing in L3 touches the engine (SHOULD).
- **R-RES-7 (new)**: chunk count bounds of section 4 (64 chunks, 64 IDAT, one of each ancillary, tEXt ≤ 256 bytes;
  resource 4 MB + 64 KB). **Ruled**: accepted.
- Colour type 3 with PLTE, and `tRNS` in any shape: **ruled rejected** (no corpus evidence).
- `tl4_pixel_budget`: R-RES-4's per-style budget as a core function the consumer calls before decoding; the budget
  instance belongs to the consumer (the adapter: one per `THEME_FILE`).

## 6. Tests, corpus, fuzzing

- Fixtures: a tiny PNG writer in `loader/tests/pngbuild.c` (signature, chunks with CRC; IDAT contents are arbitrary
  bytes since nothing decodes them) builds every test input (R-PROC-1).
- Unit (`test_png.c`): the three corpus chunk orders accepted; one negative per rule (bad signature, IHDR not first,
  IHDR length, each IHDR parameter outside the set, side 8,193, pixels over 8 M, CRC off by one bit in each chunk
  kind, repeated gAMA, tEXt without NUL, tEXt non-printable, tEXt 257 bytes, ancillary after IDAT, IDAT split by a
  gAMA, IDAT total over 4 MB, 65 chunks, unknown type `iCCP`, IEND not last, trailing byte, missing IEND, PLTE and
  tRNS in every shape; and MUST 5: gAMA 0, pHYs unit 2, sRGB 4, an empty keyword, an 80-byte keyword, a zero-length
  chunk claimed past the end, length 0x80000000, the 64/65 chunk edges); `tl4_png_idat_enum` returns the ranges in
  order and runs the whole check again (a flipped CRC fails it);
  the budget arithmetic at its edges; cleared info on every reject; determinism.
- Limits: an image of 8,192 × 1,024 with 4 MB of IDAT in 58 chunks (64 chunks in all, the bound) walks inside the fuzz
  timeout; one more IDAT byte is a reject.
- Corpus (`corpus_v4.c`): walk every IMAGE and STREAM of every style; new columns `images_ok` (must equal images +
  streams), `max_pixels`, `max_idat`, `pixels_total`; the maxima line gains the image maxima.
- Fuzz (S2): `fuzz_png`: exact-size copy, determinism (field compare), promises: on success `pixels == width *
  height`, `idat_bytes <= size`, every IDAT range inside the buffer and contiguous, counts within the limits; on
  failure the info is cleared. Two input forms from day one (MUST 4): raw bytes, and a structured form in which the
  harness builds the PNG from (type, length, data) items and computes every CRC, so the campaign spends its time past
  the CRC check without any CRC switch in the core. Seeds: the generated fixtures of the three corpus orders.

## 7. Open points

1. GDI+/libpng behaviour on gAMA and sRGB (HYPOTHESIS: rendering unaffected if stripped); irrelevant with the
   amendment of section 5, which keeps them.
2. Whether `tRNS` on colour type 2 and `PLTE` with type 3 ever appear in a third-party style: no evidence; stay as
   section 3.2 says.
3. The STREAM atlases (3 per style, up to 345 KB, 1,237,800 pixels) go through the same walker; how `draw.c` uses an
   atlas rectangle is Stanley's inventory, not L3.
4. Where the budget instance lives and how it resets (per `THEME_FILE`, per process): I1.

## 8. Evidence

Measurement scripts over the read-only work copies (research-dwm, 2026-11-15): chunk types, lengths, orders, CRCs,
IHDR sets, IDAT counts and maxima, as quoted in section 2; `docs/design/V4-LOADER-SKETCH.md` section 3 (IHDR set on
all 30,171 images); SOURCE pngsup.cpp:25-32 (GDI+ path), dll/win32/windowscodecs/CMakeLists.txt (libpng, zlib).
No image or byte of the corpus is reproduced here or anywhere in the repository (R-PROC-1).
