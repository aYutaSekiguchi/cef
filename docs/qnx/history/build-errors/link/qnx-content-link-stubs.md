# QNX content/ link stubs

## Summary

After QNX began building the desktop UI Views source block and the CEF
shared library link started, `cefsimple` failed with many undefined
references. The first few missing symbols were in `content/`:

- `content::responsiveness::NativeEventObserver::RegisterObserver()` /
  `DeregisterObserver()` — only implemented for Linux/ChromeOS, Windows,
  and Android/Fuchsia/iOS.
- `content::RendererMainPlatformDelegate::{PlatformInitialize,
  PlatformUninitialize, EnableSandbox, ctor, dtor}` — no QNX source file
  was selected by `content/renderer/BUILD.gn`.
- `content::GetFontList_SlowBlocking()` — `font_list_fontconfig.cc` is
  excluded for QNX by an earlier patch, but no QNX replacement source
  was provided.

This fix adds minimal QNX implementations so the `content/` targets link.

## Patches

```text
cef/patch/patches/qnx/chromium/content_qnx_link_stubs.patch
```

### New files

```text
cef/patch/qnx/chromium/new_files/content/renderer/renderer_main_platform_delegate_qnx.cc
cef/patch/qnx/chromium/new_files/content/common/font_list_qnx.cc
```

## Details

- `native_event_observer.cc`: widen the
  `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS)` guard around
  `RegisterObserver`/`DeregisterObserver` to also include
  `BUILDFLAG(IS_QNX)`. QNX uses Ozone and has
  `ui::PlatformEventSource`, so the Linux implementation is reused.

- `content/renderer/BUILD.gn`: add
  `renderer_main_platform_delegate_qnx.cc` to the `renderer` target for
  `is_qnx`. The stub matches the Fuchsia delegate (empty init/uninit and
  `EnableSandbox()` returns `true`), because QNX disables the Linux
  sandbox.

- `content/common/BUILD.gn`: add `font_list_qnx.cc` for `is_qnx`. The
  stub returns an empty `base::ListValue`, mirroring the Fuchsia fallback.

## Failure signatures

```text
libcef.so: undefined reference to `content::responsiveness::NativeEventObserver::RegisterObserver()'
libcef.so: undefined reference to `content::RendererMainPlatformDelegate::PlatformInitialize()'
libcef.so: undefined reference to `content::GetFontList_SlowBlocking()'
```

## Verification

```bash
autoninja -C out/qnx_release cefsimple
```

This removes the `content/` category undefined references; other
platform categories (views, gfx, net, crashpad, printing, etc.) are
handled separately.

## Search hints

```bash
rg -n "NativeEventObserver::RegisterObserver|RendererMainPlatformDelegate|GetFontList_SlowBlocking|content_qnx_link_stubs" docs/qnx/history/build-errors
```
