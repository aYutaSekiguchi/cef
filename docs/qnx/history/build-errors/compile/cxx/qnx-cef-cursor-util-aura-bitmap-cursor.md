# QNX CEF cursor_util_aura BitmapCursor include guard

## Failure signature

Stage: compile
Category: cxx
Target: `obj/cef/libcef_static/cursor_util_aura.o`

Primary diagnostics:

```text
../../cef/libcef/browser/native/cursor_util_aura.cc:117:24: error: no type named 'BitmapCursor' in namespace 'ui'
using CursorType = ui::BitmapCursor;
                   ~~~~^
../../cef/libcef/browser/native/cursor_util_aura.cc:118:48: error: unknown type name 'CursorType'
../../cef/libcef/browser/native/cursor_util_aura.cc:142:17: error: unknown type name 'CursorType'; did you mean 'ui::mojom::CursorType'?
../../cef/libcef/browser/native/cursor_util_aura.cc:130:17: error: use of undeclared identifier 'CursorType'
```

## Root cause

QNX builds CEF with Aura/Ozone. `cursor_util_aura.cc` selects the generic Ozone cursor implementation with:

```cpp
#elif BUILDFLAG(IS_OZONE)
using CursorType = ui::BitmapCursor;
```

However, the include for `ui/ozone/common/bitmap_cursor.h` was nested under `BUILDFLAG(IS_LINUX)`. QNX therefore reached the Ozone code path without the `ui::BitmapCursor` declaration.

## Fix

Direct CEF source change:

- Extend the Ozone cursor include guard in `cef/libcef/browser/native/cursor_util_aura.cc` from Linux-only to Linux-or-QNX:
  - `#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)`
  - update the matching `#endif` comment.

## Verification

```text
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
ninja -C out/qnx_release obj/cef/libcef_static/cursor_util_aura.o
EXIT:0
```

## Search hints

```bash
rg -n "BitmapCursor|cursor_util_aura|FromPlatformCursor|IS_OZONE|SUPPORTS_OZONE_X11" docs/qnx/history/build-errors
```
