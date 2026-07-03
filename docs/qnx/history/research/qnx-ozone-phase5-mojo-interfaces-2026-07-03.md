# QNX Ozone Phase 5 — Mojo Interfaces: QNX-local DMAbuf Frame Transport

**Date:** 2026-07-03
**Phase:** 5, first substep
**Status:** Complete — mojom generates and compiles; qnx source_set links

## Scope

Add compile-safe QNX-local Mojo interface definitions and minimal generated-code wiring for Browser↔GPU DMAbuf frame transport. Do NOT implement GPU producer runtime, DMAbuf export/import logic, or Screen display code.

---

## Files Changed

### New files under `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`

| File | Lines | Purpose |
|---|---|---|
| `mojom/BUILD.gn` | 21 | GN target for `qnx_gpu_mojom` mojom generation |
| `mojom/qnx_gpu.mojom` | 126 | QNX-local mojom interfaces and struct definitions |

### Modified files under `cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`

| File | Change | Purpose |
|---|---|---|
| `BUILD.gn` | Added `import("//mojo/public/tools/bindings/mojom.gni")`; added `mojom("qnx_gpu_mojom")` block; added `:qnx_gpu_mojom` to `source_set("qnx")` deps | Include mojom target; wire as dep of qnx source_set |

---

## Mojom Schema Summary

**Module:** `ui.ozone.qnx.mojom`

### Data Structures

#### `QnxDmaBufPlane`
```mojom
struct QnxDmaBufPlane {
  handle<platform> fd;    // Mojo platform handle wrapping a DMAbuf fd
  uint32 stride;           // bytes per scan line
  uint64 offset;          // byte offset from fd start where plane begins
  uint64 size;             // total size of this plane in bytes
};
```
- Each DMAbuf plane carries one `handle<platform>` — Mojo serializes this as a POSIX fd via its platform handle transport (underlying mechanism: SCM_RIGHTS on POSIX).
- Avoids `gfx::NativePixmapHandle` dependency at this early stage.
- Matches the Phase 2 design sketch (`NativePixmapPlane`-like but QNX-local only).

#### `QnxDmaBufFrame`
```mojom
struct QnxDmaBufFrame {
  uint32 widget;           // gfx::AcceleratedWidget target
  uint32 generation;       // GPU connection generation (crash-recovery counter)
  uint32 width;
  uint32 height;
  uint32 fourcc;           // e.g. 0x34325241 for AR24
  uint64 modifier;         // DRM modifier (0 = linear)
  array<QnxDmaBufPlane> planes;
};
```
- Complete metadata for browser to import each plane via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` and display the frame.
- Matches Phase 1B evidence: AR24 single-plane, modifier=0 under QEMU virgl.

### Interfaces

#### `QnxGpuHost` (GPU → Browser)
```mojom
interface QnxGpuHost {
  SubmitFrame(QnxDmaBufFrame frame) => (bool accepted, string diagnostic);
  ReportProducerLost(uint32 widget, uint32 generation);
};
```
- Browser holds the receiver/host side; GPU process is the client.
- `SubmitFrame` sends a rendered frame with DMAbuf fds as Mojo platform handles.
- `ReportProducerLost` notifies browser that GPU producer crashed.

#### `QnxGpuControl` (Browser → GPU)
```mojom
interface QnxGpuControl {
  AttachWidget(uint32 widget, uint32 generation, gfx.mojom.Size size);
  ResizeWidget(uint32 widget, uint32 generation, gfx.mojom.Size size);
  DetachWidget(uint32 widget, uint32 generation);
};
```
- GPU process holds the receiver/control side; Browser is the client.
- `AttachWidget` maps browser widget to GPU render producer.
- `ResizeWidget` triggers producer buffer recreation.
- `DetachWidget` cleans up producer on window close or shutdown.

---

## Validation

### Commands run

```bash
# Apply Phase 3 GN patches (needed for ozone_platform_qnx flag)
cd /home/yuta/chromium/src
patch -p0 < cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
patch -p0 < cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch

# Copy new_files into root Chromium tree
cp -r cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/* \
   ui/ozone/platform/qnx/

# Generate build
source /home/yuta/qnx800/qnxsdp-env.sh
gn gen out/qnx_phase5_mojo --args="$(cat ../out/qnx_release/args.gn; echo 'ozone_platform_qnx=true')"

# Build mojom target
ninja -C out/qnx_phase5_mojo phony/ui/ozone/platform/qnx/qnx_gpu_mojom

# Build qnx source_set
ninja -C out/qnx_phase5_mojo obj/ui/qnx/libqnx_ui.a

# Verify generated files
ls gen/ui/ozone/platform/qnx/mojom/
```

### Build output

```
[8604/8604] CXX obj/ui/ozone/platform/qnx/qnx_gpu_mojom/qnx_gpu.mojom.o
```
- ✅ `qnx_gpu.mojom.cc` compiled (42.2 KB)
- ✅ `qnx_gpu.mojom.o` linked (180.3 KB)
- ✅ `qnx_gpu.mojom-shared.o` compiled (35.1 KB)
- ✅ `libqnx_ui.a` built (all Phase 4 source files: ozone_platform_qnx.o, qnx_screen_context.o, qnx_window_manager.o, qnx_window.o, qnx_screen.o, qnx_platform_event_source.o)

### Generated artifacts

```
gen/ui/ozone/platform/qnx/mojom/
├── qnx_gpu.mojom.cc              # Interface dispatch (Proxy/Stub)
├── qnx_gpu.mojom.h               # Public C++ class definitions
├── qnx_gpu.mojom-shared.cc       # Struct serialization (Serialize/Deserialize)
├── qnx_gpu.mojom-shared.h        # Struct trait implementations
├── qnx_gpu.mojom-forward.h       # Forward declarations
├── qnx_gpu.mojom-import-headers.h
├── qnx_gpu.mojom-params-data.h   # Parameter encoding
├── qnx_gpu.mojom-data-view.h     # DataView classes
├── qnx_gpu.mojom-send-validation.h
├── qnx_gpu.mojom-shared-internal.h
├── qnx_gpu.mojom-shared-message-ids.h
└── qnx_gpu.mojom-test-utils.h
```

### Git diff check

```bash
git diff --check build/config/ozone.gni ui/ozone/BUILD.gn
# EXIT: 0 (no whitespace errors)
find ui/ozone/platform/qnx -type f -exec git diff --check {} \;
# EXIT: 0 for all files
```

### Cleanup

- `build/config/ozone.gni` and `ui/ozone/BUILD.gn` reverted via `git checkout`
- `ui/ozone/platform/qnx/` root-source directory removed (new_files remain in `cef/patch/qnx/chromium/new_files/`)
- Build artifacts in `out/qnx_phase5_mojo/` retained (local build artifacts, not in source tree)

---

## What Remains for Phase 5 Subsequent Substeps

### Phase 5 substep: GPU producer implementation
- `QnxSurfaceFactoryOzone` — provide `GLOzone` instance for GPU process
- `QnxGLOzoneEGL` — implement `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` path
- `QnxRenderProducer` — per-widget producer: render → export DMAbuf fd → send `SubmitFrame` over Mojo
- Wire `QnxGpuHost` client binding in GPU process `InitializeGPU()`

### Phase 5 substep: Browser host binding
- `QnxWindowManager` — own `QnxGpuHost` receiver and `QnxGpuControl` remote
- `QnxWindow` — own `QnxGpuControl` per-widget binding; receive `SubmitFrame` and import DMAbuf via `eglCreateImageKHR`
- Wire `QnxGpuControl` client binding in Browser process
- `QnxGpuHost` receiver dispatch: receive frames, discard or display

### Phase 5 substep: Browser display composition
- `QnxWindow` — set up EGL display, EGLSurface, EGLContext for display composition
- Implement GLES2 fullscreen-quad render with imported DMAbuf texture
- `eglSwapBuffers()` to Screen window
- Return `accepted=true/false` in `SubmitFrame` callback

---

## Blockers / Requested Plan Updates

**None for this substep.** The mojom compiles and the source_set links successfully.

### Deferred decisions for parent consideration

1. **Mojo binding pattern**: The Phase 2 design sketch uses `GpuPlatformSupportHost` for browser-side Mojo binding. Should Phase 5 substeps wire `QnxGpuHost`/`QnxGpuControl` through the existing `GpuPlatformSupportHost` interface (like `CreateStubGpuPlatformSupportHost()` currently used), or should the binding be done more directly in `QnxWindowManager` via `MojoEnvironmentFilterChannelParams`? The existing Phase 4 stub uses `CreateStubGpuPlatformSupportHost()`.

2. **Interface versioning**: The mojom is at version 0 (unversioned). When should a stable version be declared? Phase 5 should remain unversioned (v0), migrate to `[Stable]` once Phase 6 crash recovery is validated.

3. **Multi-plane support**: The mojom supports `array<QnxDmaBufPlane>` for multi-plane formats (e.g., NV12). Should Phase 5 producer also handle multi-plane YUV or defer to Phase 7?

4. **Forward declaration vs. full include**: The generated mojom includes `ui/gfx/geometry/mojom/geometry.mojom.h` for `gfx::Size`. Should the Phase 5 header wrapper (if added) use forward declaration or full include?
