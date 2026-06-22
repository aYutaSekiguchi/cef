# QNX payments browser-bound key desktop implementation

## Summary

`libcef.so` linked a payments caller of:

```text
payments::GetBrowserBoundKeyStoreInstance(payments::BrowserBoundKeyStore::Config)
```

but QNX was not included in the payments browser-binding `is_desktop` platform
set, so neither Android nor desktop implementation sources were built.

## Root cause

`components/payments/content/browser_binding/BUILD.gn` defines:

```gn
is_desktop = is_mac || is_win || is_linux || is_chromeos || is_fuchsia
```

QNX reaches desktop Chrome/CEF payments code but was excluded from this set. The
desktop implementation is suitable as a QNX fallback: it asks crypto for an
unexportable-key provider and gracefully returns no hardware-backed key support
when no provider exists.

## Fix

Patch:

```text
cef/patch/patches/qnx/chromium/payments_browser_bound_key_qnx.patch
```

Add `is_qnx` to `is_desktop` so `browser_bound_key_store_desktop.cc` supplies
`GetBrowserBoundKeyStoreInstance()`.

## Verification

```bash
source out/qnx_release/qnx_env.sh
buildtools/linux64/gn --root=. -q --regeneration gen out/qnx_release
autoninja -C out/qnx_release cefsimple
```

Observed `GN_EXIT:0`; `grep 'undefined reference' /tmp/cefsimple_build.log |
grep 'payments::'` returned no lines. Distinct undefined references dropped from
100 to 99.

## Search hints

```bash
rg -n "GetBrowserBoundKeyStoreInstance|BrowserBoundKeyStore|payments_browser_bound_key" docs/qnx/history/build-errors
```
