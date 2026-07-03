# QNX Ozone Phase 3 validation

**Date:** 2026-07-03

## Scope

Validate the Phase 3 GN/Ozone wiring after the Phase 3 review and stub fix.

## Validated changes

- CEF-managed patches:
  - `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch`
  - `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch`
- CEF-managed new files:
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.{cc,h}`
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.{cc,h}`
- Patch registration:
  - `patch/patch.cfg`

## Commands run

From `/home/yuta/chromium/src`, patches were applied temporarily and new files copied into `ui/ozone/platform/qnx/`. Cleanup then removed those temporary root-source changes.

```sh
gn gen out/qnx_phase3 --args="$(cat out/qnx_release/args.gn)
ozone_platform_qnx = true
"
```

Result:

```text
Done. Made 33067 targets from 4277 files in 2664ms
```

Then the opt-in QNX Ozone target was built:

```sh
ninja -C out/qnx_phase3 ui/ozone/platform/qnx:qnx
```

Result:

```text
[10050/10100] CXX obj/ui/ozone/platform/qnx/qnx/client_native_pixmap_factory_qnx.o
[10051/10100] CXX obj/ui/ozone/platform/qnx/qnx/ozone_platform_qnx.o
[10100/10100] AR obj/ui/ozone/libozone_base.a
```

The target completed successfully. The high action count was because `out/qnx_phase3` was a fresh validation output directory; no long full Chromium target was requested.

Additional checks:

```sh
git diff --check -- cef/patch/patch.cfg \
  cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn \
  cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc \
  cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch \
  cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
```

Result: exit 0, no whitespace errors.

Patch dry-run checks also passed with `patch -p0 --dry-run --batch --forward` for both GN patches.

## Result

Phase 3 GN/Ozone wiring is validated:

- `ozone_platform_qnx` exists and is opt-in (`false` by default).
- `gn gen` succeeds when temporarily enabling `ozone_platform_qnx=true`.
- The minimal QNX Ozone stub target compiles.
- Default headless behavior remains unchanged because no QNX platform flag is enabled by default and the bootstrap script does not set `ozone_platform_qnx=true`.

## Remaining work

Proceed to Phase 4: Browser/UI-side QNX Ozone skeleton (`QnxScreenContext`, `QnxWindowManager`, `QnxWindow`, `QnxPlatformEventSource`, and minimal Mojo plumbing).