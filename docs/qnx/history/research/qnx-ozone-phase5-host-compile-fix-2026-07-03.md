# Phase 5 Bounded Compile-Fix Report — 2026-07-03

## Files changed (CEF-managed source under `patch/qnx/chromium/new_files/`)

Three files in `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/` were edited:

### `qnx_gl_ozone_egl.cc` — `NOTREACHED_IN_MIGRATION` → `NOTREACHED()`

**Problem**: `NOTREACHED_IN_MIGRATION()` is not declared in Chromium at this
compatibility tag. The QNX Ozone scaffold had used this macro to mark the
`CreateViewGLSurface` path as intentionally unreachable in the GPU process.

**Fix**: Replaced `NOTREACHED_IN_MIGRATION()` with `NOTREACHED()` on line 48,
which is provided by `base/notreached.h` (already included transitively via
`ui/gl/gl_surface_egl.h`).

```cpp
// Before:
NOTREACHED_IN_MIGRATION()
    << "QnxGLOzoneEGL: GPU process cannot create a window surface; "
       "use CreateOffscreenGLSurface";

// After:
NOTREACHED()
    << "QnxGLOzoneEGL: GPU process cannot create a window surface; "
       "use CreateOffscreenGLSurface";
```

### `qnx_render_producer.h` — EGL include ordering

**Problem**: `qnx_render_producer.h` conditionally included `<EGL/eglext.h>`
under a `#ifndef EGLuint64KHR` guard, without first including `<EGL/egl.h>`.
When the Chromium build mapped `<EGL/*.h>` to `third_party/angle/include/EGL/*.h`,
`eglext.h` was compiled without base EGL types (`EGLDisplay`, `EGLenum`,
`EGLBoolean`), producing unknown-type errors.

**Fix**: Replaced the conditional guard with a direct `#include <EGL/egl.h>`
followed by `#include <EGL/eglext.h>`, matching the pattern used by
`ui/gl/gl_surface_egl.h` and `ui/ozone/platform/drm/gpu/gbm_surfaceless.h`.

```cpp
// Before:
#ifndef EGLuint64KHR
#include <EGL/eglext.h>
#endif

// After:
// Include EGL base types before EGL extension types.
// This matches the pattern used by ui/gl/gl_surface_egl.h and
// ui/ozone/platform/drm/gpu/gbm_surfaceless.h: include egl.h first,
// then eglext.h.  The Chromium build system maps <EGL/*.h> to
// third_party/khronos/EGL/*.h (or third_party/angle/include/EGL/*.h)
// so these are safe to use as system-style includes.
#include <EGL/egl.h>
#include <EGL/eglext.h>
```

### `qnx_gpu_host.cc` — `ReportProducerLost` diagnostic off-by-one

**Problem**: After calling `SetGpuAttached(widget, false, 0)` and
`IncrementGeneration(widget)`, the diagnostic log at the end of
`ReportProducerLost` logged `(record->generation + 1)`, one more than the
newly committed value. Since `record` is a `const QnxWidgetRecord*` pointing
to the map entry that was already incremented, `record->generation` already
holds the post-increment value.

**Fix**: Captured the pre-increment generation in a local variable and
logged both old and new values.

```cpp
// Before:
window_manager_->SetGpuAttached(widget, false, 0);
window_manager_->IncrementGeneration(widget);
DLOG(INFO) << "...generation incremented to "
           << (record->generation + 1);

// After:
// Capture old generation before mutating the record so the log
// is accurate.
uint32_t old_generation = record->generation;
window_manager_->SetGpuAttached(widget, false, 0);
window_manager_->IncrementGeneration(widget);
DLOG(INFO) << "...generation incremented from "
           << old_generation << " to " << record->generation;
```

## Validation

### Commands

**1. Git diff check on CEF new_files:**
```sh
git diff patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc \
         patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.h \
         patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc
```
Result: No whitespace errors. All three edits confirmed in files.

**2. Apply Phase 3 patches to parent Chromium tree** (two edits to
`build/config/ozone.gni` and `ui/ozone/BUILD.gn`):
- Added `ozone_platform_qnx = false` to `build/config/ozone.gni`
- Added `ozone_platform_qnx` to the assert in `build/config/ozone.gni`
- Added `if (ozone_platform_qnx) { ozone_platforms += ["qnx"]; ozone_platform_deps += ["platform/qnx"]; }` to `ui/ozone/BUILD.gn`

**3. Copy CEF new_files into parent Chromium tree:**
```sh
mkdir -p /home/yuta/chromium/src/ui/ozone/platform/qnx
cp -r /home/yuta/chromium/src/cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/* \
      /home/yuta/chromium/src/ui/ozone/platform/qnx/
```

**4. GN generation:**
```sh
cd /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_host_compile_fix --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```
Result: `Done. Made 33099 targets from 4278 files in 2827ms` — succeeded.

**5. Ninja build:**
```sh
cd /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_host_compile_fix ui/ozone/platform/qnx:qnx
```
Result: `798/798` targets built — **exit 0, succeeded**. Confirmed object files:
- `obj/ui/ozone/platform/qnx/qnx/qnx_gl_ozone_egl.o` (35.9K)
- `obj/ui/ozone/platform/qnx/qnx/qnx_render_producer.o` (139.0K)
- `obj/ui/ozone/platform/qnx/qnx/qnx_gpu_host.o` (84.6K)

**6. Cleanup:**
- Removed temporary `ui/ozone/platform/qnx/` from parent Chromium tree
- Removed `out/qnx_phase5_host_compile_fix/` validation directory
- Reverted Phase 3 patch edits in parent Chromium tree (`git checkout -- build/config/ozone.gni ui/ozone/BUILD.gn`)
- Parent Chromium tree verified clean

## Result

**`ui/ozone/platform/qnx:qnx` compiles successfully** after the three targeted fixes.

## AddInterfaces / Browser Host Binding — Unresolved

The Phase 5 Mojo host audit (`qnx-ozone-phase5-mojo-host-audit-2026-07-03.md`)
identified a **design-level blocker** for the browser-side `QnxGpuHost` binding:
`OzonePlatform::AddInterfaces` is called from Chromium's GPU-exposed-interface
path (`content::ExposeGpuInterfacesToBrowser`), not from the browser process.
The current `has_initialized_ui() && qnx_gpu_host_` guard in `ozone_platform_qnx.cc`
therefore makes registration a no-op in out-of-process GPU runs, and the browser
would have no `QnxGpuHost` receiver.

This compile-fix microtask **did not address** that design issue. The compile
fixes are complete, but the `AddInterfaces` process-direction blocker remains
**unresolved** until a separate design microtask defines the correct browser-side
`QnxGpuHost` receiver binding path (e.g., via `GpuHostBinder` or a dedicated
browser-owned Mojo interface registration mechanism).

## Residual Risks

- **`AddInterfaces` wrong process direction**: The current scaffold does not
  bind `QnxGpuHost` in the browser process for out-of-process GPU runs.
  A separate design microtask is required.
- **`QnxGpuControl` attach/generation wiring absent**: `QnxWidgetRecord`
  starts with `generation=0` and `gpu_attached=false`; `SubmitFrame` always
  rejects frames. No real frame can be displayed until `QnxGpuControl`
  implements attach/resize/detach.
- **`QnxGpuHost::Bind` single receiver**: If GPU restart can create a second
  receiver while the first is still bound, a `mojo::ReceiverSet` is needed.
  This is deferred until runtime crash/restart is wired.

## Recommended Next Step

Separate design microtask to define and implement the correct browser-owned
`QnxGpuHost` receiver binding path for out-of-process GPU. Do not accept the
current `AddInterfaces` approach without that design review.
