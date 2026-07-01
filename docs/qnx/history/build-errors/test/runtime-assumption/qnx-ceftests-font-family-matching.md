# qnx-ceftests-font-family-matching

- Stage: test
- Category: runtime-assumption
- First observed: 2026-07-01 (production-ready replacement for the DejaVu
  workaround described in
  `docs/qnx/history/archive/qnx-ceftests-text-zero-width-dejavu-workaround.md`)
- Affected target: `ceftests` on QNX, any test that depends on text
  layout driven by font metrics (notably `DownloadTest.Clicked*`,
  `DisplayTest.AutoResize`, and many `CorsTest` cases that depend on
  rendered text width / vertical metrics).

## Signature

- Without fontconfig wired up, inline elements that wrap text reported a
  `0x0` bounding rect:
  - `<a href="https://test-download.com/download.txt">CLICK ME</a>`
    had `anchorRect={"x":8,"y":8,"w":0,"h":0}`.
  - `document.elementFromPoint(20, 20)` returned `BODY` instead of the
    `<a>`, so dispatched click events never reached the link.
- Net effect: many tests that simulate a user click or measure content
  size fail at the layout step, not at the navigation step.
- After this fix, every requested family is mapped through the
  fontconfig-backed `SkFontMgr`, so the rendered typeface is whatever the
  QEMU fontconfig cache resolves to, not a hard-coded DejaVu stub. Layout
  metrics (ascent/descent/line-box width) reflect that real face.

## Root cause

QNX SDP 8 ships no usable `libfontconfig.so` (the bundled one lacks
`FcWeightFromOpenType` and friends). With Chromium 147 rebase, the QNX
port stopped masquerading as GN `is_linux`, so:

1. `//skia/BUILD.gn`'s local `if (is_linux || is_chromeos)` guards for
   `skia_ports_fci_sources` (the Skia `SkFontConfigInterface_direct`
   family) and the `//third_party/fontconfig` dep stopped pulling those
   sources in for QNX. The result is that `libskia.a` does not contain
   the FCI implementation.
2. `skia/ext/font_utils.cc`'s `fontmgr_factory()` only routes to the
   fontconfig path on `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)`.
   On QNX it falls through to `SkFontMgr::RefEmpty()`, so
   `DefaultFontMgr()` returns an empty font manager.
3. Blink's `FontCache::CreateTypeface` then resolves every CSS family to
   `nullptr`, which yields an empty `SkTypeface` with zero
   ascent/descent. Inline line-boxes collapse and `elementFromPoint`
   cannot hit-test anchors.

The previous DejaVu workaround (`font_cache_linux_qnx.cc` override)
masked the symptom but bypassed family-based matching entirely, so
every CSS family resolved to the same DejaVu face.

## Fix

Wire up Linux's fontconfig path on QNX, the same way Chromium on Linux
uses it. Six coordinated changes:

### Chromium side

1. `//skia/BUILD.gn`:
   - Extend the local `if (is_linux || is_chromeos)` guards for
     `skia_ports_fci_sources` and the `//third_party/fontconfig` dep
     to `if (is_linux || is_chromeos || is_qnx)` so QNX pulls the FCI
     sources into `libskia.a`.
   - Add a new `else if (is_qnx)` block in the `skia_config` target
     that defines `SK_BUILD_FOR_UNIX` (so Skia's platform auto-detect
     skips the macOS branch), `SK_GAMMA_EXPONENT=1.2`, and
     `SK_GAMMA_CONTRAST=0.2` (matching Linux).
2. `//skia/ext/font_utils.cc`: add `BUILDFLAG(IS_QNX)` to the Linux/ChromeOS
   branch in `fontmgr_factory()` so QNX calls
   `SkFontConfigInterface::RefGlobal()` and routes through
   `SkFontMgr_New_FCI`.
3. `//third_party/fontconfig/BUILD.gn`:
   - Add `|| is_qnx` to the `assert` and to the `libs = ["uuid"]`
     guard. QNX SDP 8 ships no `libuuid`, so the `libs` line must be
     skipped on QNX to avoid an `-luuid: No such file or directory`
     link error.
4. `//third_party/fontconfig/fontconfig.gni`: add `|| is_qnx` to the
   `assert` and to the `use_bundled_fontconfig` default so the in-tree
   fontconfig library is selected when the bootstrap runs.
5. `//content/common/BUILD.gn`:
   - Drop `is_qnx` from the `if (is_linux || is_chromeos) && !is_qnx`
     exclusion so `font_list_fontconfig.cc` is compiled on QNX.
   - Add `if (is_qnx) deps += ["//third_party/fontconfig"]` so QNX links
     the in-tree fontconfig.
6. `//third_party/blink/renderer/platform/BUILD.gn`,
   `//third_party/blink/renderer/platform/fonts/font_cache.h`,
   `//third_party/blink/renderer/platform/fonts/skia/font_cache_skia.cc`,
   `//ui/gfx/BUILD.gn`:
   - Use Linux's `font_cache_linux.cc`, `font_fallback_linux.cc`,
     `font_render_params_linux.cc`, `fontconfig_util.cc`,
     `content/common/font_list_fontconfig.cc` on QNX too. Drop the
     `#if !BUILDFLAG(IS_QNX)` guard around `CreateTypeface` in
     `font_cache_skia.cc`. Add `BUILDFLAG(IS_QNX)` to the
     `font_fallback_linux.h` include guard in `font_cache.h`.

### CEF side

- Add `use_bundled_fontconfig = true` to `args.gn` in
  `tools/cef_create_projects_qnx.sh` (SDP 8's
  `/lib/libfontconfig.so.1.16.0` lacks `FcWeightFromOpenType` and friends).
- Delete the QNX font stubs (workaround-only files, not QNX-specific
  implementations of underlying APIs):
  - `patch/qnx/chromium/new_files/content/common/font_list_qnx.cc`
  - `patch/qnx/chromium/new_files/skia/ext/font_utils_qnx.cc`
  - `patch/qnx/chromium/new_files/third_party/blink/renderer/platform/fonts/linux/font_cache_linux_qnx.cc`
  - `patch/qnx/chromium/new_files/ui/gfx/font_fallback_qnx.cc`
  - `patch/qnx/chromium/new_files/ui/gfx/font_render_params_qnx.cc`
- Delete (the patches are kept, just rebuilt without the stub additions):
  - `patch/patches/qnx/chromium/content_common_font_list_fontconfig_qnx.patch`
  - `patch/patches/qnx/chromium/third_party_blink_renderer_platform_BUILD_stubs_is_qnx.patch`
  - `patch/patches/qnx/chromium/ui_gfx_font_fallback_render_qnx.patch`

## Verification

- `./out/qnx_release/ninja_qnx.sh ceftests` succeeds on a clean
  `cef_create_projects_qnx.sh` bootstrap (`git checkout -f && gclient
  sync -f -R && bootstrap`).
- `DownloadTest.*`: 31/31 pass on the new build.
- `AxViewportCollapseTest.*`: 12/12 pass.
- `FindHandlerTest.*`: 6/6 pass.
- `DraggableRegionsTest.*`: 2/2 pass.
- `CorsTest.*`: 294/297 pass (the 3 failures are pre-existing
  QNX-specific CORS policy issues on custom schemes, unrelated to
  fonts). Tests that previously relied on `AnchorRect != 0` now pass
  because the fontconfig-mapped family produces a non-zero
  ascent/descent/width.
- `nm libcef.so` shows `SkFontConfigInterface::RefGlobal`,
  `SkFontConfigInterface::GetSingletonDirectInterface`,
  `SkFontMgr_New_FCI`, and the entire `SkFontConfigInterface_direct::*`
  family as locally-defined symbols (lowercase `t`), so the fontconfig
  back-end is genuinely wired up rather than skipped.

## Cross-references

- `docs/qnx/history/archive/qnx-ceftests-text-zero-width-dejavu-workaround.md`
  is the prior DejaVu-only workaround, kept as historical context.
- `docs/qnx/build-error-index.md` search terms updated: `fontconfig_wired_qnx`,
  `font_fallback_linux_qnx_removed`, `skia_use_fontconfig_for_qnx`,
  `SkFontConfigInterface_RefGlobal_defined`, `use_bundled_fontconfig_qnx`.
- The DejaVu-specific keywords
  (`font_cache_linux_qnx|SkFontMgr_New_Fontations_Empty|zero width|BODY`)
  have been retired; new searches should use the production-ready
  terms.