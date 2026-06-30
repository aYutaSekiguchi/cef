# qnx-ceftests-text-zero-width

- Stage: test
- Category: runtime-assumption
- First observed: 2026-06-30
- Affected target: `ceftests` on QNX, any test that depends on text layout
  (notably `DownloadTest.Clicked*`, `DisplayTest.AutoResize`, and many
  `CorsTest` cases that depend on rendered text width).

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
shipped a stub that returned `nullptr` for both `SystemFontFamily()` and
`PlatformFallbackFontForCharacter()`. The default Skia font manager on QNX
is `SkFontMgr_New_Custom_Empty()` (no system fonts), so any text run fell
through to `skia::DefaultFont()` and then to a typeface with no metrics.
Layout reported `0` ascent/descent/width.

## Fix

The QNX port now resolves the first readable font file from
`/usr/share/fonts/` (DejaVu, Roboto, etc.) and uses
`SkTypeface_Factory::FromFilenameAndTtcIndex()` to instantiate the typeface
from `PlatformFallbackFontForCharacter()`. `FontPlatformData` is built with a
sane size (defaults to `14` when `FontDescription::ComputedSize()` is `0`) so
`SimpleFontData::PlatformInit()` measures ascent/descent and `getMetrics()`
returns real numbers.

`SystemFontFamily()` returns `"DejaVu Sans"` so Blink accepts the system
family lookup without falling through to a hard-coded empty string.

## Verification

- `ninja_qnx.sh ceftests` builds successfully.
- `DownloadTest.ClickedRCNone`: link rect changes from `0x0` to
  `{"x":8,"y":23,"w":76,"h":0}`. Width is recovered; height is still `0`,
  which is the symptom of a separate line-box layout issue unrelated to font
  metrics. Tests that depend on click hit-testing or content-resize still
  fail; this fix is the prerequisite for resolving them.
- No regression: `AxViewportCollapseTest.*` (12/12) and
  `CorsTest.{IframeNoneServerToServer,RedirectPost307ServerToServer}` pass.

## Notes

- Do not weaken `DownloadTest.ClickedRCNone` or `DisplayTest.AutoResize` by
  changing click coordinates or text content; the next iteration must make
  the inline box height match the font ascent/descent.
- This change does not wire Skia to fontconfig. If a future iteration wants
  name-based font matching, it should add `font_fallback_linux.cc` and link
  `//third_party/fontconfig` for the QNX toolchain.