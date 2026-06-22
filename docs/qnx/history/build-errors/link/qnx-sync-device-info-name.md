# QNX sync device info local name helper

## Summary

`libcef.so` linked sync/session code that calls:

```text
syncer::GetPersonalizableDeviceNameInternal()
```

but `components/sync_device_info` had no QNX platform source for this helper.

## Root cause

`components/sync_device_info/BUILD.gn` selects platform-specific
`local_device_info_util_*.cc` files for Android, ChromeOS, iOS, Linux, Fuchsia,
Mac, and Windows. QNX was missing, so `local_device_info_util.cc` declared but
could not link `GetPersonalizableDeviceNameInternal()`.

## Fix

Patch and new file:

```text
cef/patch/patches/qnx/chromium/sync_device_info_qnx.patch
cef/patch/qnx/chromium/new_files/components/sync_device_info/local_device_info_util_qnx.cc
```

Add a QNX implementation that returns `gethostname()` when available and `"QNX"`
as a fallback.

## Verification

```bash
source out/qnx_release/qnx_env.sh
autoninja -C out/qnx_release cefsimple
```

Observed no `syncer::` undefined references. Distinct undefined references
dropped from 99 to 98.

## Search hints

```bash
rg -n "GetPersonalizableDeviceNameInternal|local_device_info_util_qnx|sync_device_info_qnx" docs/qnx/history/build-errors
```
