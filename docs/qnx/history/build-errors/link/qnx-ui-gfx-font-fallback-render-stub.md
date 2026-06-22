# QNX ui/gfx font fallback and render params stubs

## Summary

`ui/gfx/font_fallback_linux.cc` and `ui/gfx/font_render_params_linux.cc`
provide the `gfx::GetFallbackFont`, `gfx::GetFallbackFonts`, and
`gfx::GetFontRenderParams*` Linux implementations. Both are only built
for `is_linux || is_chromeos` and pull in `//third_party/fontconfig`.
QNX has neither the source selection nor the fontconfig dep, so
`libcef.so` fails to link with:

```text
undefined reference to `gfx::GetFallbackFont(...)'
undefined reference to `gfx::GetFallbackFonts(...)'
undefined reference to `gfx::GetFontRenderParams(...)'
undefined reference to `gfx::GetFontRenderParamsDeviceScaleFactor()'
```

## Patches

```text
cef/patch/patches/qnx/chromium/ui_gfx_font_fallback_render_qnx.patch
```

### New files

```text
cef/patch/qnx/chromium/new_files/ui/gfx/font_fallback_qnx.cc
cef/patch/qnx/chromium/new_files/ui/gfx/font_render_params_qnx.cc
```

## Details

- `font_fallback_qnx.cc`: returns an empty `std::vector<Font>` from
  `GetFallbackFonts` and `false` from `GetFallbackFont`.
- `font_render_params_qnx.cc`: returns a default-constructed
  `FontRenderParams` from `GetFontRenderParams`, an empty cache-clearing
  hook, and `1.0f` from `GetFontRenderParamsDeviceScaleFactor`.
- `ui/gfx/BUILD.gn`: add the two stubs to the `gfx` component when
  `is_qnx`.

## Verification

```bash
autoninja -C out/qnx_release cefsimple
```

Removes the `gfx::GetFallback*` and `gfx::GetFontRenderParams*`
undefined references; other gfx/ui/views categories are handled
separately.

## Search hints

```bash
rg -n "GetFallbackFont|GetFontRenderParams|ui_gfx_font_fallback_render_qnx" docs/qnx/history/build-errors
```
