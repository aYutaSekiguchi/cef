# QNX Ozone Phase 5: Render Producer Lifecycle — 2026-07-03

## Task

Wire GPU-side `QnxGpuService` AttachWidget/ResizeWidget/DetachWidget to `QnxRenderProducer` lifecycle, and add a metadata/export-only `SubmitFrame` exercise path. Keep scope compile-safe; no browser EGL import/display.

## Changes Made

### 1. `QnxRenderProducerManager` — constructor with surface factory

**Files:** `qnx_render_producer.h`, `qnx_render_producer.cc`

Added a forward declaration for `QnxSurfaceFactoryOzone` and a new constructor:
```cpp
explicit QnxRenderProducerManager(QnxSurfaceFactoryOzone* surface_factory);
```

`GetOrCreateProducer` now passes `surface_factory_` to the `QnxRenderProducer` constructor instead of `nullptr`:
```cpp
auto producer = std::make_unique<QnxRenderProducer>(
    surface_factory_, widget, generation, size);
```

### 2. `QnxGpuService` — GPU-side producer lifecycle state

**Files:** `qnx_gpu_service.h`, `qnx_gpu_service.cc`

- **New members**: `raw_ptr<QnxSurfaceFactoryOzone> surface_factory_` and `std::unique_ptr<QnxRenderProducerManager> producer_manager_`
- **New constructor**: `explicit QnxGpuService(QnxSurfaceFactoryOzone* surface_factory)` — creates `producer_manager_` with the factory
- **Destructor**: calls `producer_manager_->RemoveAllProducers()` before resetting `gpu_host_remote_`
- **`AttachWidget`**: calls `producer_manager_->GetOrCreateProducer(widget, gen, size)` then `Initialize()` on the producer; logs init result
- **`ResizeWidget`**: removes then recreates producer for the same generation (handles race where resize arrives before attach)
- **`DetachWidget`**: calls `producer_manager_->RemoveProducer(widget, gen)`
- **`SubmitTestFrameForWidget`**: calls `producer_manager_->GetProducer`, then `CreateExportFrame()`, then converts and submits to `gpu_host_remote_->SubmitFrame` if valid; guarded by null checks at every step
- **`NativeFrameToMojomFrame`** (private static): converts `ui::QnxDmaBufFrame` (with `base::ScopedFD` fds) to `ui::ozone::qnx::mojom::QnxDmaBufFramePtr` (with `mojo::PlatformHandle` fds) by constructing `QnxDmaBufPlanePtr` with in-place construction and `mojo::PlatformHandle` wrapping. Uses `ScopedFD::get()` + new `ScopedFD` + move to `PlatformHandle` to avoid double-close on the `const ScopedFD&` input. Namespace disambiguation uses `::ui::QnxDmaBufFrame` for the native type.

### 3. `OzonePlatformQnxImpl` — wire `QnxGpuService` with surface factory

**File:** `ozone_platform_qnx.cc`

- `InitializeGPU`: now creates `gpu_service_ = std::make_unique<QnxGpuService>(gpu_surface_factory_.get())` after creating `gpu_surface_factory_`. Uses `std::unique_ptr<QnxGpuService>` member (no more leaked raw pointer).
- `AddInterfaces`: uses `gpu_service_.get()` to bind the `QnxGpuService` and `QnxGpuControl` receivers to the already-created service. Guards with null check and informative DLOG.
- Updated stale comment in `QnxSurfaceFactoryOzone` header (removed incorrect "Phase 4 territory" claim about Screen ownership).
- `QnxSurfaceFactoryOzone` comment updated to accurately describe GPU-side ownership.

### 4. Stale comment fixes

- `QnxGpuService` class comment updated: removed "Tiny accessor and logging only" and "deferred to later Phase 5 substep" (these were Phase 5 substep 4 comments now superseded)
- `ozone_platform_qnx.cc` `InitializeGPU` comment updated: replaced "no real GPU producer resources" and "QnxGpuService is lazily created in AddInterfaces" with accurate "creates GPU service with access to surface factory"

## Validation

### Whitespace check
```bash
git diff --check cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/
```
Result: **passed** — no whitespace errors.

### Root-source temporary validation

```bash
# Apply Phase 3 GN patches
git apply -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
git apply -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch

# Copy CEF new_files to root source
mkdir -p ui/ozone/platform/qnx
cp -R cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/

# GN generation
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_render_lifecycle \
  --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```
Result: `Done. Made 33099 targets from 4278 files in 2663ms`

```bash
# Ninja build
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_render_lifecycle \
    ui/ozone/platform/qnx/mojom:mojom \
    ui/ozone/platform/qnx:qnx
```
Result: **798/798 targets compiled successfully.** All object files present:
- `qnx_gpu_service.o` (96KB)
- `qnx_render_producer.o` (144KB)
- `ozone_platform_qnx.o` (166KB)

### Post-validation cleanup
- `ui/ozone/platform/qnx/` removed from root source tree.
- `out/qnx_phase5_render_lifecycle/` retained (no source tree pollution).
- GN patches reverted; root Chromium GN files confirmed clean.

## Compile Target Status

| Target | Status |
|--------|--------|
| `ui/ozone/platform/qnx/mojom:mojom` | ✅ pass |
| `ui/ozone/platform/qnx:qnx` (all 798 targets) | ✅ pass |

## API / Lifecycle Summary

### `QnxRenderProducerManager`
```
QnxRenderProducerManager(QnxSurfaceFactoryOzone* surface_factory)
  → stores surface_factory_
GetOrCreateProducer(widget, gen, size)
  → creates QnxRenderProducer(surface_factory_, ...)
RemoveProducer(widget, gen)
  → map.erase(key)
GetProducer(widget, gen)
  → map.find(key)
RemoveAllProducers()
  → map.clear()
```

### `QnxGpuService` — lifecycle methods
```
AttachWidget(widget, gen, size):
  producer_manager_->GetOrCreateProducer(...)
  producer->Initialize()  // if not yet initialized

ResizeWidget(widget, gen, size):
  producer_manager_->RemoveProducer(widget, gen)
  producer_manager_->GetOrCreateProducer(widget, gen, size)
  producer->Initialize()  // if not yet initialized

DetachWidget(widget, gen):
  producer_manager_->RemoveProducer(widget, gen)

SubmitTestFrameForWidget(widget, gen):
  if producer exists and is valid:
    frame, err = producer->CreateExportFrame()
    if frame has planes:
      mojom_frame = NativeFrameToMojomFrame(frame)
      gpu_host_remote_->SubmitFrame(mojom_frame, callback)

Destructor:
  producer_manager_->RemoveAllProducers()  // destroys all producers
```

### `OzonePlatformQnxImpl` — GPU process wiring
```
InitializeGPU():
  gpu_surface_factory_ = make_unique<QnxSurfaceFactoryOzone>()
  gpu_service_ = make_unique<QnxGpuService>(gpu_surface_factory_.get())

AddInterfaces() [GPU process]:
  binders->Add<QnxGpuService>(gpu_service_.get(), ...)
  binders->Add<QnxGpuControl>(gpu_service_.get(), ...)
```

## GPU-Side Render Producer Resources

After this Phase 5 substep, `QnxGpuService` creates/resizes/destroys `QnxRenderProducer` instances on Attach/Resize/Detach:

| Browser call | GPU action |
|---|---|
| `AttachWidget(w, g, size)` | Creates `QnxRenderProducer` for w/g, calls `Initialize()` |
| `ResizeWidget(w, g, size)` | Removes old, creates new `QnxRenderProducer` for w/g |
| `DetachWidget(w, g)` | Removes `QnxRenderProducer` for w/g |
| GPU process exit | `QnxGpuService` destructor calls `RemoveAllProducers()` |
| `SubmitTestFrameForWidget(w, g)` | Calls `CreateExportFrame()`, submits to `QnxGpuHost` |

## SubmitFrame / Export-Only Exercise Path

`SubmitTestFrameForWidget(widget, generation)` exists as a compile-safe export-only exercise path. It calls `QnxRenderProducer::CreateExportFrame()` (which creates a real DMAbuf DRM image via `eglCreateDRMImageMESA` and exports fds via `eglExportDMABUFImageMESA`), converts the native frame to mojom with real fd handles, and calls `gpu_host_remote_->SubmitFrame`. The browser host returns `accepted=false` with `"QNX host: VALID metadata; accepted=false; import/display deferred"`.

Remaining items for real browser import/display and runtime smoke:
- **Browser EGL import/display**: `QnxGpuHost::SubmitFrame` returns `accepted=false`; actual `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` import, GL texture binding, Screen window composition, and `eglSwapBuffers()` are not implemented
- **Runtime out-of-process GPU smoke**: Requires a real `cefsimple --ozone-platform=qnx` run under QEMU virgl with no `--in-process-gpu`
- **Crash/restart runtime smoke**: `QnxWindowManager::DetachAllWidgets()` and `QnxGpuHost::ReportProducerLost()` are wired in Phase 5 host scaffold; runtime crash smoke is still pending
- **`SubmitTestFrameForWidget` invocation**: Currently defined but not yet called from a GPU render loop; needs a periodic call site or explicit trigger
- **Full render pipeline wiring**: `QnxRenderProducer::CreateExportFrame()` creates a demo DRM image but does not yet copy GPU framebuffer content into it

## Files Changed

| File | Change |
|------|--------|
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.h` | Added forward declaration for `QnxSurfaceFactoryOzone`; changed `QnxRenderProducerManager` constructor to take `QnxSurfaceFactoryOzone*`; added `surface_factory_` member |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.cc` | Updated `QnxRenderProducerManager` constructor and `GetOrCreateProducer` to use `surface_factory_` instead of `nullptr` |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.h` | Added forward declaration for `QnxDmaBufFrame`; added `surface_factory_`, `producer_manager_` members; added `SubmitTestFrameForWidget` declaration; updated class comment |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc` | Full rewrite: constructor takes `surface_factory*` and creates `producer_manager_`; `AttachWidget`/`ResizeWidget`/`DetachWidget` manage producers; `SubmitTestFrameForWidget` exercises export path; `NativeFrameToMojomFrame` converts native→mojom frame |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc` | `InitializeGPU` creates `gpu_service_` as `unique_ptr` with surface factory; `AddInterfaces` uses `gpu_service_.get()`; stale comments fixed |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_factory.h` | Comment update: removed incorrect "Phase 4 territory" claim |
