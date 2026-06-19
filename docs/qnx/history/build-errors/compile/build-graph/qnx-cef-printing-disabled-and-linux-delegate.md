# QNX CEF printing-disabled build and Linux platform delegate reuse

## Failure signature

Stage: compile
Category: build-graph
Targets:

- `obj/cef/libcef_static/browser_guest_util.o`
- `obj/cef/libcef_static/alloy_browser_host_impl.o`
- `obj/cef/libcef_static/browser_platform_delegate_create.o`
- `obj/cef/libcef_static/print_util.o`

Primary diagnostics:

```text
../../chrome/browser/printing/print_preview_dialog_controller.h:14:10: fatal error: 'components/printing/common/print.mojom.h' file not found
../../components/printing/browser/print_composite_client.h:16:10: fatal error: 'components/printing/common/print.mojom.h' file not found
../../cef/libcef/browser/browser_platform_delegate_create.cc:28:2: error: A delegate implementation is not available for your platform.
../../cef/libcef/browser/browser_platform_delegate_create.cc:48:17: error: unknown type name 'CefBrowserPlatformDelegateOsr'
../../components/printing/browser/print_manager.h:12:10: fatal error: 'components/printing/common/print.mojom.h' file not found
```

A follow-up after guarding the printing includes was:

```text
../../cef/libcef/browser/alloy/alloy_browser_host_impl.cc:83:13: error: no member named 'kChromeUIPrintHost' in namespace 'chrome'
```

## Root cause

QNX CEF builds set:

```gn
enable_print_preview = false
enable_printing = false
```

Consequently `components/printing/common/print.mojom.h` is not generated and `chrome::kChromeUIPrintHost` is not declared. CEF still included print-preview/printing headers unconditionally in Alloy guest handling and cross-process-subframe printing code.

Separately, `browser_platform_delegate_create.cc` only selected native/OSR delegate implementations for Win/Mac/Linux. QNX uses the same Aura/Ozone non-X11 path as the Linux delegate implementation, but the CEF build graph did not add those Linux delegate sources for QNX.

## Fix

Direct CEF source/build changes:

- `libcef/browser/browser_guest_util.cc`
  - include `printing/buildflags/buildflags.h`
  - include `print_preview_dialog_controller.h` only when `ENABLE_PRINT_PREVIEW`
  - return `nullptr` from the print-preview helper when print preview is disabled
- `libcef/browser/alloy/alloy_browser_host_impl.cc`
  - include `print_composite_client.h` only when `ENABLE_PRINTING`
  - no-op `PrintCrossProcessSubframe()` when printing is disabled
  - include `chrome::kChromeUIPrintHost` in the WebUI allowlist only when `ENABLE_PRINT_PREVIEW`
- `libcef/browser/printing/print_util.cc`
  - include `print_view_manager` / PDF print utility headers only when `ENABLE_PRINTING`
  - make `Print()` a no-op and report `PrintToPDF is disabled` via the callback when printing is disabled
- `libcef/browser/browser_platform_delegate_create.cc`
  - route QNX through the Linux native/OSR delegate classes
- `BUILD.gn`
  - for `is_qnx`, add the Linux native/OSR delegate sources and `util_linux.{cc,h}`
  - add `//third_party/angle:libEGL` for the non-X11 Ozone path
  - do not add `print_dialog_linux.{cc,h}` because QNX disables printing

## Verification

```text
./out/qnx_release/ninja_qnx.sh \
  obj/cef/libcef_static/browser_guest_util.o \
  obj/cef/libcef_static/alloy_browser_host_impl.o \
  obj/cef/libcef_static/browser_platform_delegate_create.o \
  obj/cef/libcef_static/browser_platform_delegate_native_linux.o \
  obj/cef/libcef_static/browser_platform_delegate_osr_linux.o
EXIT:0

./out/qnx_release/ninja_qnx.sh obj/cef/libcef_static/print_util.o
EXIT:0
```

## Search hints

```bash
rg -n "print.mojom.h|kChromeUIPrintHost|browser_platform_delegate_create|CefBrowserPlatformDelegateOsr|ENABLE_PRINT_PREVIEW|ENABLE_PRINTING" docs/qnx/history/build-errors
```
