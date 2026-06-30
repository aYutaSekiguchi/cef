# qnx-ceftests-text-zero-width

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, any test that depends on text layout
  (notably `DownloadTest.Clicked*`, `DisplayTest.AutoResize`, and many
  `CorsTest` cases that depend on rendered text width / vertical metrics).

## Signature

- Inline elements that wrap text reported a `0x0` bounding rect.
  Example from `DownloadTest.ClickedRCNone`:
  - `<a href="https://test-download.com/download.txt">CLICK ME</a>`
    had `anchorRect={"x":8,"y":8,"w":0,"h":0}`.
  - `document.elementFromPoint(20, 20)` returned `BODY` instead of the
    `<a>`, so dispatched click events never reached the link.
- Net effect: many tests that simulate a user click or measure content size
  fail at the layout step, not at the navigation step.

## Root cause

`third_party/blink/renderer/platform/fonts/linux/font_cache_linux_qnx.cc`
shipped a stub that returned `nullptr` for `SystemFontFamily()` and
`PlatformFallbackFontForCharacter()`. The default Skia font manager on
QNX is `SkFontMgr_New_Custom_Empty()` (no system fonts installed), so:

1. Family-based lookup in `CreateTypeface` (defined in
   `skia/font_cache_skia.cc`) calls `skia::DefaultFontMgr()->matchFamilyStyle()`
   which returns `nullptr` for every CSS family. The default Skia path
   therefore yields an empty `SkTypeface`.
2. `SimpleFontData::PlatformInit` reads `SkFont::getMetrics()` from that
   empty typeface and reports zero ascent, descent, and line spacing.
3. With zero metrics, Blink's inline line-box collapses to height 0 and
   `elementFromPoint` cannot hit-test anchors.

`PlatformFallbackFontForCharacter` only handles character fallback for
unrendered code points; it does not influence the primary typeface that
Blink uses for layout, so it cannot recover the inline metrics.

## Fix

Three coordinated QNX-only changes:

1. Add `FontCache::CreateTypeface` to
   `third_party/blink/renderer/platform/fonts/linux/font_cache_linux_qnx.cc`
   that always returns a real system font (DejaVu Sans, shipped on the
   QEMU image) regardless of the requested family name. `name` is
   rewritten to "DejaVu Sans" so downstream code sees a stable identity.
2. Add `#if !BUILDFLAG(IS_QNX)` guards around `CreateTypeface` in
   `third_party/blink/renderer/platform/fonts/skia/font_cache_skia.cc`
   so the Skia default implementation is not compiled in on QNX.
3. Update `third_party/blink/renderer/platform/BUILD.gn` so QNX still
   includes `font_cache_skia.cc` for the remaining helpers
   (`CreateFontPlatformData`, `GetLastResortFallbackFont`) but the
   `CreateTypeface` symbol is taken from the QNX file.

The DejaVu Sans typeface comes from
`SkTypeface_Factory::FromFilenameAndTtcIndex("/usr/share/fonts/DejaVuLGCSans.ttf", 0)`.
`PlatformFallbackFontForCharacter` also returns DejaVu-based
`SimpleFontData` so missing-glyph fallback still has real metrics.
`SystemFontFamily()` returns the non-empty string `"DejaVu Sans"` so
Blink accepts the system family lookup.

## Verification

- `./out/qnx_release/ninja_qnx.sh ceftests` succeeds.
- `DownloadTest.*`: 31/31 pass (`CT_DL_ALL2_EC:0`). The previously failing
  `ClickedRCNone` etc. now reach the `OnBeforeDownload`, `OnDownloadUpdated`,
  and `DownloadComplete` callbacks, and the temporary file receives the
  expected `kTestContent`.
- `AxViewportCollapseTest.*` (12/12) and the previously isolated
  `CorsTest.RedirectPost307ServerToServer` continue to pass.

## Notes

- Do not weaken `DownloadTest.ClickedRCNone` or `DisplayTest.AutoResize` by
  changing click coordinates or text content; the next iteration should
  focus on the remaining `FrameHandlerTest` cross-origin-nav timing
  failures and the `DisplayTest.AutoResize` font-metric expectation
  (DejaVu reports `width=63` where the test expects `50±1`).
- Do not bypass the underlying hit-test failure in the shared
  `SendJavaScriptClickEvent` test helper (selecting the first `<a>` and
  calling `element.click()` instead of dispatching a `MouseEvent` at the
  original coordinates). That drops button/modifier/client-coordinate
  semantics for every caller of the helper and hides the real layout
  bug; any fix for `DownloadTest.*` must be per-test if it is needed at
  all.
- This change does not wire Skia to fontconfig. If a future iteration
  wants name-based font matching, it should add `font_fallback_linux.cc`
  and link `//third_party/fontconfig` for the QNX toolchain, instead of
  collapsing every CSS family to "DejaVu Sans".