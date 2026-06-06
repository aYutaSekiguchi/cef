# libdrm on QNX needs an explicit memstream link dependency and a makedev wrapper

- Date: 2026-06-05
- Signature: open_memstream undeclared or makedev arity mismatch in xf86drm.c
- Stage: link
- Category: build-graph
- Scope: third_party/libdrm

## Symptoms

- `third_party/libdrm/src/xf86drm.c` failed on QNX with:
  - `call to undeclared function 'open_memstream'`
  - `too few arguments provided to function-like macro invocation` for `makedev`
- Even after the header side was understood, the final QNX build also required an explicit link against `libmemstream`.

## Root cause

- libdrm assumed the glibc/Linux API surface:
  - `open_memstream()` visible from `<stdio.h>` and linked from libc
  - `makedev(major, minor)` taking two arguments
- QNX differs in both places:
  - `open_memstream()` is declared in `<sys/memstream.h>`
  - the implementation lives in `libmemstream`, so the library dependency must be present in GN
  - `makedev` is a three-argument macro on QNX

## Fix pattern

- Treat header visibility and final link dependency as separate concerns.
- When QNX provides the feature in a separate library, capture that dependency in GN instead of relying on libc-like behavior.
- Wrap API-shape differences such as `makedev` behind a local helper macro rather than forking all call sites semantically.

## Applied change

- Added a QNX-only `#include <sys/memstream.h>` in `xf86drm.c`.
- Added a `DRM_MAKEDEV(major, minor)` wrapper that expands to the QNX three-argument `makedev` form.
- Replaced direct `makedev(...)` call sites with `DRM_MAKEDEV(...)`.
- Added a QNX-only `libs = [ "memstream" ]` dependency in `third_party/libdrm/BUILD.gn`.

## Verification

- `autoninja -C out/qnx_release obj/third_party/libdrm/libdrm/xf86drm.o` succeeded.
- `gn desc out/qnx_release //third_party/libdrm:modetest libs` included `memstream`.
- The fix became reproducible through the normal `cef_create_projects_qnx.sh` flow.

## Files touched

- `third_party/libdrm/src/xf86drm.c`
- `third_party/libdrm/BUILD.gn`
- `cef/patch/patches/qnx/chromium/libdrm_qnx_memstream_makedev.patch`
- `cef/patch/patch.cfg`

## Related notes

- `docs/qnx/build-error-index.md`
