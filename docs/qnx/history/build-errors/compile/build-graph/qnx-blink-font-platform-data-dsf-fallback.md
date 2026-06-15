# QNX: Blink `FontCache::DeviceScaleFactor()` is not declared outside Linux/ChromeOS

- Date: 2026-06-15
- Signature: `no member named 'DeviceScaleFactor' in 'blink::FontCache'`, `error: font_platform_data.cc:241`
- Stage: compile
- Category: build-graph
- Scope: `third_party/blink/renderer/platform/fonts/font_platform_data.cc` (single call site, one-line guard)

## Symptoms

- `out/qnx_release/ninja_qnx.sh cef` aborts at step 4283/44956 with a single failure:
  `FAILED: obj/third_party/blink/renderer/platform/platform/font_platform_data.o`
- The clang error reads:
  ```
  ../../third_party/blink/renderer/platform/fonts/font_platform_data.cc:241:20: error: no member named 'DeviceScaleFactor' in 'blink::FontCache'
    241 |         FontCache::DeviceScaleFactor(), &result);
        |                    ^~~~~~~~~~~~~~~~~
  ```
- `args.gn` is unchanged from a previously good `base_unittests` build; only the wider
  `cefsimple` target brings in this file. A clean re-bootstrap of the same checkout
  reproduces the failure.

## Root cause

`FontCache::DeviceScaleFactor()` (and the `device_scale_factor_` static member it
returns) are gated on `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)` in
`third_party/blink/renderer/platform/fonts/font_cache.h`:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  static float DeviceScaleFactor() { return device_scale_factor_; }
  static void SetDeviceScaleFactor(float device_scale_factor) { ... }
#endif
```

The call site in `font_platform_data.cc:241` sits inside a block gated on
`!IS_ANDROID && !IS_FUCHSIA && !IS_IOS`, so on QNX (which is none of those) the
body is reached but the method declaration is missing.

`services/screen_ai` is the same shape handled correctly in
`font_description.cc:320`:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  float device_scale_factor_for_key = FontCache::DeviceScaleFactor();
#else
  float device_scale_factor_for_key = 1.0f;
#endif
```

The TODO comment above the call site in `font_platform_data.cc` (`crbug.com/1327530`)
explicitly says "Linux/Cros are the only platforms that adjust these settings by
different device scale factors", which matches the upstream pattern: only Linux
and ChromeOS use a real DSF; every other platform uses `1.0f`.

## Fix pattern

Mirror the `font_description.cc:320` guard pattern so every non-Linux/ChromeOS
platform (including QNX) calls the sandbox-render-style helper with a `1.0f`
literal. The DSF-based anti-alias / subpixel-positioning adjustments that
`FontCache::DeviceScaleFactor()` enables on Linux are documented in the TODO as
"Linux/Cros only", so 1.0f is the correct value for QNX.

## Applied change

Added the `IS_LINUX || IS_CHROMEOS` guard at the call site in
`font_platform_data.cc:241`:

```cpp
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)
  Platform::Current()->GetSandboxSupport()->GetWebFontRenderStyleForStrike(
      family.data(), text_size, is_bold, is_italic,
      FontCache::DeviceScaleFactor(), &result);
#else
  Platform::Current()->GetSandboxSupport()->GetWebFontRenderStyleForStrike(
      family.data(), text_size, is_bold, is_italic, 1.0f, &result);
#endif
```

The change is captured in
`cef/patch/patches/qnx/chromium/blink_font_platform_data_qnx_dsf_fallback.patch`
and registered in `cef/patch/patch.cfg`. The patch was regenerated with
`patch_updater.py --resave --patch=qnx/chromium/blink_font_platform_data_qnx_dsf_fallback`
and verified to apply cleanly on a clean tree via `patch -p0 --batch --dry-run`.

## Verification

- `patch -p0 --batch --dry-run` on a clean source tree exits 0 with
  `checking file third_party/blink/renderer/platform/fonts/font_platform_data.cc`.
- The full cefsimple build progressed past step 4283 (the original failure point)
  and reached step 1803/38151 in a subsequent run before exposing a different
  failure (`screen_ai` `kServiceSandbox` missing — see the
  `qnx-mojom-is-qnx-enabled-features` note).
- No behavior change on Linux/ChromeOS (the existing branch is preserved verbatim).
- On QNX the call now uses the same `1.0f` default as every other
  non-Linux/ChromeOS platform, so text rendering on QNX matches the upstream
  fallback semantics.

## Files touched

- `cef/patch/patches/qnx/chromium/blink_font_platform_data_qnx_dsf_fallback.patch` (new)
- `cef/patch/patch.cfg` (registered the new patch in apply order)

## Related notes

- `docs/qnx/patch-hygiene.md` — patch format verification, regenerate-from-tree workflow
- `docs/qnx/history/build-errors/compile/build-graph/qnx-mojom-is-qnx-enabled-features.md` —
  the screen_ai `kServiceSandbox` failure that surfaced immediately after this fix
