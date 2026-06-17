# QNX: shell_integration GetDirectLaunchUrlScheme

## Stage

- stage: compile
- category: build-graph / platform-guard
- target: `obj/chrome/browser/ui/startup/impl/google_chrome_scheme_util.o`

## Failure signature

```text
../../chrome/browser/ui/startup/google_chrome_scheme_util.cc:70:26: error: no member named 'GetDirectLaunchUrlScheme' in namespace 'shell_integration'
   70 |       shell_integration::GetDirectLaunchUrlScheme();
      |                          ^~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

## Root cause

`google_chrome_scheme_util.cc` calls `shell_integration::GetDirectLaunchUrlScheme()`
on all non-Android, non-iOS desktop platforms, which includes QNX. The function
declaration in `chrome/browser/shell_integration.h` and the Linux implementation
in `chrome/browser/shell_integration_linux.{h,cc}` were both gated to
Win/Mac/Linux/ChromeOS, so QNX compiled the call site without the symbol.

## Fix

Extend the declaration guard in `chrome/browser/shell_integration.h` to include
`BUILDFLAG(IS_QNX)`. In `chrome/browser/BUILD.gn`, also include
`shell_integration_linux.{cc,h}` when `is_linux || is_qnx` so the QNX build
links the same implementation as Linux. The implementation is small and does
not depend on Linux-specific APIs.

## Verification

```bash
cd /home/yuta/chromium/test/src
ninja -C out/qnx_release \
  obj/chrome/browser/ui/startup/impl/google_chrome_scheme_util.o
```

Result: object build completed successfully.

## Search hints

```bash
rg -n "GetDirectLaunchUrlScheme|google_chrome_scheme_util|shell_integration" docs/qnx/history/build-errors/compile
```
