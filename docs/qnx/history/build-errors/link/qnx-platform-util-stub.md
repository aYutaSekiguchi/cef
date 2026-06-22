# QNX platform_util link stubs

## Summary

`libcef.so` linked QNX desktop/browser code that references platform shell
helpers, but QNX had no platform implementation in the `chrome/browser` target:

```text
platform_util::internal::PlatformOpenVerifiedItem(base::FilePath const&, platform_util::OpenItemType)
platform_util::ShowItemInFolder(Profile*, base::FilePath const&)
platform_util::OpenExternal(GURL const&)
```

## Root cause

`chrome/browser/platform_util_linux.cc` is only added under the Linux DBus path
(`use_dbus`) and depends on DBus/xdg portal integrations. QNX does not enable
that stack, so the cross-platform callers from `platform_util.cc` had no QNX
backend.

## Fix

Patch and new file:

```text
cef/patch/patches/qnx/chromium/platform_util_qnx_stub.patch
cef/patch/qnx/chromium/new_files/chrome/browser/platform_util_qnx.cc
```

Add `platform_util_qnx.cc` to `chrome/browser:browser` when `is_qnx`. The QNX
implementation is intentionally no-op until a real QNX shell/file-manager/URL
handler integration exists. `OpenItem()` still validates path/type and completes
its callback in `platform_util.cc`; the QNX backend simply avoids invoking an
external shell.

## Verification

```bash
source out/qnx_release/qnx_env.sh
buildtools/linux64/gn --root=. -q --regeneration gen out/qnx_release
autoninja -C out/qnx_release cefsimple
```

Observed `GN_EXIT:0`; `grep 'undefined reference' /tmp/cefsimple_build.log |
grep 'platform_util::'` returned no lines. Distinct undefined references dropped
from 103 to 100.

## Search hints

```bash
rg -n "PlatformOpenVerifiedItem|ShowItemInFolder|OpenExternal|platform_util_qnx" docs/qnx/history/build-errors
```
