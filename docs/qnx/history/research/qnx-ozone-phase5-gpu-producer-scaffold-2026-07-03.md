# Phase 5 Substep: GPU-side QNX Ozone Render Producer/Export Scaffold

**Date:** 2026-07-03
**Parent:** `qnx-ozone-phase5-mojo-fix-2026-07-03.md`
**Status:** ✅ Build-clean; scaffold compiled successfully

## Goal

Add compile-safe GPU-side QNX Ozone classes for the Phase 1B-proven
`eglCreateDRMImageMESA` → `eglExportDMABUFImageMESA` DMAbuf export pipeline.
Validation: `ninja -C out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx` must
succeed with zero errors.

---

## What was done

### New scaffold files (in `ui/ozone/platform/qnx/`)

| File | Purpose |
|------|---------|
| `qnx_surface_factory.h` / `.cc` | `SurfaceFactoryOzone` subclass owned by `OzonePlatformQnxImpl::InitializeGPU()`. Returns `QnxGLOzoneEGL` for GLES2. `CreateNativePixmap` returns nullptr (runtime deferred). |
| `qnx_gl_ozone_egl.h` / `.cc` | `GLOzoneEGL` subclass. `GetNativeDisplay()` returns `EGL_DEFAULT_DISPLAY` (Phase 1B-proven path). `LoadGLES2Bindings()` delegates to `LoadDefaultEGLGLES2Bindings()`. `CreateViewGLSurface()` is `NOTREACHED()` (GPU has no Screen window). `CreateOffscreenGLSurface()` returns `PbufferGLSurfaceEGL`. |
| `qnx_render_producer.h` / `.cc` | Owns GPU-side EGL display and EGL function pointers for the DRM-image path. Key methods: `CreateDRMImage()`, `ExportDmaBufImage()`, `QueryDmaBufMetadata()`. Runtime Mojo binding deferred. |

### Mojom interfaces

| File | Purpose |
|------|---------|
| `mojom/qnx_gpu.mojom` | `QnxDmaBufPlane`, `QnxDmaBufFrame` structs; `QnxGpuHost` (GPU→Browser) and `QnxGpuControl` (Browser→GPU) interfaces. |

### GN wiring changes

| File | Change |
|------|--------|
| `build/config/ozone.gni` | Added `ozone_platform_qnx = false` declare_args + QNX in platform list |
| `ui/ozone/BUILD.gn` | Added `if (ozone_platform_qnx)` conditional → `"platform/qnx"` |
| `ui/ozone/platform/qnx/BUILD.gn` | New target `source_set("qnx")` with all source files + `screen/EGL/GLESv2` libs + `//ui/ozone/platform/qnx/mojom` dep |
| `mojom/BUILD.gn` | New mojom target for `qnx_gpu.mojom` |

### `ozone_platform_qnx.cc` changes

- `OzonePlatformQnxImpl::InitializeGPU()` creates `gpu_surface_factory_` (a
  `QnxSurfaceFactoryOzone`).
- `GetSurfaceFactoryOzone()` returns `gpu_surface_factory_.get()` (nullptr
  before `InitializeGPU` runs, which is correct for the headless/browser path).

---

## Compilation errors resolved

| Error | Fix |
|-------|-----|
| `QnxGLOzoneEGL` defined outside `namespace ui {}` | Added `namespace ui { }` wrapper in header |
| Missing `base/single_thread_task_runner.h` | Removed stale include from `qnx_render_producer.h` |
| Missing `ui/ozone/common/egl_util.h` and `base/check.h` in `qnx_gl_ozone_egl.cc` | Added both |
| EGL type forward declarations conflicting with real EGL types | Replaced with `#include "third_party/khronos/EGL/egl.h"` + `#include "third_party/khronos/EGL/eglext.h"` |
| `NOTREACHED_IN_MIGRATION` unresolved | Replaced with `NOTREACHED()` (not defined in this Chromium version) |
| `GL_NO_ERROR` / `GL_INVALID_ENUM` undefined | Added `#include "ui/gl/gl_bindings.h"` (provides GL constants via QNX SDK sysroot headers) |
| `GLDisplayEGL::GetInstanceForCurrentPlatform` not found | Changed to `GLDisplayEGL::GetDisplayForCurrentContext()` (correct API) |
| `eglGetProcAddress` return type cannot cast to `void*` | Template now declares `Fn proc = reinterpret_cast<Fn>(eglGetProcAddress(name))` directly instead of going through `void*` intermediate |

---

## Build result

```
$ ninja -C out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx
[796/798] AR obj/ui/ozone/libozone_base.a
[797/798] AR obj/ui/base/ime/libime_shared_mojom_traits.a
[798/798] AR obj/ui/platform_window/libplatform_window.a
```

No errors. `ui/ozone/platform/qnx/mojom:mojom` also builds clean independently.

---

## Open questions / deferred items

1. **`CreateNativePixmap`** in `QnxSurfaceFactoryOzone` returns nullptr. The
   Phase 5 follow-up substep should wire `QnxRenderProducer::CreatePixmap()`
   so that `SharedImage`-backed `NativePixmap` creation works from the GPU
   process.

2. **Mojo binding** of `QnxGpuHost` / `QnxGpuControl` in the OzonePlatform
   is deferred. `ozone_platform_qnx.cc` has the `GpuPlatformSupportHost`
   stub placeholder; the real Mojo message pipe needs to be connected in the
   browser-side and GPU-side entry points.

3. **EGL display lifetime** — `QnxRenderProducer` falls back to
   `eglGetDisplay(EGL_DEFAULT_DISPLAY)` if `GLDisplayEGL::GetDisplayForCurrentContext()`
   returns nullptr. On real QNX hardware the Phase 1B probes proved this
   fallback works. Confirm with QEMU test once Phase 6 runtime is wired.

---

## Files changed

```
Modified (CEF-managed bootstrap patches already in-tree):
  build/config/ozone.gni                              +4/-1
  ui/ozone/BUILD.gn                                  +5/-0

New files (CEF patch sources under cef/patch/qnx/chromium/new_files/):
  ui/ozone/platform/qnx/BUILD.gn                     new
  ui/ozone/platform/qnx/mojom/BUILD.gn               new
  ui/ozone/platform/qnx/mojom/qnx_gpu.mojom           new
  ui/ozone/platform/qnx/qnx_surface_factory.h        new
  ui/ozone/platform/qnx/qnx_surface_factory.cc        new
  ui/ozone/platform/qnx/qnx_gl_ozone_egl.h           new
  ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc          new
  ui/ozone/platform/qnx/qnx_render_producer.h          new
  ui/ozone/platform/qnx/qnx_render_producer.cc        new

Modified (by bootstrap / hand-applied gni patches):
  ui/ozone/platform/qnx/ozone_platform_qnx.cc        +GPU wiring
  ui/ozone/platform/qnx/ozone_platform_qnx.h         +fwd decl
```

---

## Recommended next step

Phase 5 third substep: wire `QnxRenderProducer` runtime into the Ozone
platform — specifically, connect `QnxRenderProducer::CreatePixmap()` as the
`CreateNativePixmap` implementation and add the Mojo message pipe between
GPU `QnxGpuHost` client and browser `QnxGpuControl` receiver. See
`docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md` § "Mojo bridge design".
