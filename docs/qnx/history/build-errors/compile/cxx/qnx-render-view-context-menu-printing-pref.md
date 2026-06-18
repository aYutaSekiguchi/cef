# QNX: RenderViewContextMenu printing pref guard

## Stage

- stage: compile
- category: cxx / undeclared pref symbol
- target: `obj/chrome/browser/browser/render_view_context_menu.o`

## Failure signature

```text
../../chrome/browser/renderer_context_menu/render_view_context_menu.cc:4091:58: error: no member named 'kPrintingEnabled' in namespace 'prefs'
 4091 |     return GetPrefs(browser_context_)->GetBoolean(prefs::kPrintingEnabled) &&
```

## Root cause

QNX builds with `enable_printing=false`, so
`printing/buildflags/buildflags.h` defines `ENABLE_PRINTING` as `0` and
`prefs::kPrintingEnabled` is not declared in `chrome/common/pref_names.h`.

Most print-menu code in `render_view_context_menu.cc` is already guarded by
`BUILDFLAG(ENABLE_PRINTING)`, but the Glic-specific print-preview branch in
`RenderViewContextMenu::IsPrintPreviewEnabled()` referenced
`prefs::kPrintingEnabled` outside that guard.

## Fix

Wrap the Glic print-preview branch in `IsPrintPreviewEnabled()` with:

```cpp
#if BUILDFLAG(ENABLE_PRINTING)
...
#endif
```

This keeps QNX's no-printing build from referencing printing-only prefs while
preserving the existing behavior on printing-enabled platforms.

## Verification

```bash
cd /home/yuta/chromium/test/src
QNX_HOST=$QNX_HOST QNX_TARGET=$QNX_TARGET \
  ninja -C out/qnx_release \
  obj/chrome/browser/browser/render_view_context_menu.o
```

Result: `EXIT:0`; no C++ errors emitted.

## Search hints

```bash
rg -n "kPrintingEnabled|IsPrintPreviewEnabled|render_view_context_menu" docs/qnx/history/build-errors
```
