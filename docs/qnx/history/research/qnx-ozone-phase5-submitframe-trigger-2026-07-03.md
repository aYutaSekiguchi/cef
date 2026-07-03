# QNX Ozone Phase 5: SubmitFrame Trigger — 2026-07-03

## Task

Implement a bounded runtime-oriented `SubmitFrame` trigger in `QnxGpuService` that fires after `AttachWidget` (and `ResizeWidget`) creates/initializes a producer. The trigger exercises the GPU→Browser Mojo `QnxGpuHost::SubmitFrame` path with a real DMAbuf export, but validation for this substep is compile-only.

## Changes Made

### 1. `qnx_gpu_service.h` — added feature flag member

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.h`

Added a scaffold-only feature flag member:

```cpp
// Feature flag for bounded SubmitFrame trigger.  When true, AttachWidget
// and ResizeWidget call SubmitTestFrameForWidget() after producer
// initialization to exercise the GPU->Browser Mojo SubmitFrame path.
// This is a scaffold-only flag; no runtime validation in this substep.
bool enable_attach_test_frame_ = true;
```

This allows the trigger to be disabled by setting `enable_attach_test_frame_ = false` if needed, without removing the call site.

### 2. `qnx_gpu_service.cc` — `AttachWidget` trigger

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc`

After producer initialization in `AttachWidget`, added:

```cpp
// ---- Phase 5: bounded SubmitFrame trigger ----
// After producer is created and (re-)initialized, exercise the GPU->Browser
// Mojo SubmitFrame path with a test frame.  This validates the fd
// ownership/move semantics in SubmitTestFrameForWidget at compile time
// and provides diagnostic output at runtime without requiring full app smoke.
// Guard: only fire if the browser host remote is bound (AttachExistingWidgets
// ensures Initialize() was called first) and the producer is valid.
if (enable_attach_test_frame_ && gpu_host_remote_ && producer->is_valid()) {
  DLOG(INFO) << "QnxGpuService::AttachWidget: trigger: calling "
                "SubmitTestFrameForWidget(widget="
             << widget << ", generation=" << generation << ")";
  SubmitTestFrameForWidget(widget, generation);
} else if (enable_attach_test_frame_ && !gpu_host_remote_) {
  DLOG(WARNING) << "QnxGpuService::AttachWidget: enable_attach_test_frame_ "
                   "is true but gpu_host_remote_ is null; skipping "
                   "SubmitTestFrameForWidget (GPU service may not be "
                   "initialized yet)";
} else if (enable_attach_test_frame_ && !producer->is_valid()) {
  DLOG(WARNING) << "QnxGpuService::AttachWidget: enable_attach_test_frame_ "
                   "is true but producer is not valid; skipping "
                   "SubmitTestFrameForWidget";
}
```

**Trigger sequence:**
1. `QnxGpuPlatformSupportHost::OnGpuServiceLaunched` → `Initialize(host_remote)` binds `gpu_host_remote_`
2. `AttachExistingWidgets` sends `AttachWidget` for each browser widget
3. `AttachWidget` creates producer → `Initialize()` → EGL extension probing
4. **Trigger:** `if (enable_attach_test_frame_ && gpu_host_remote_ && producer->is_valid())` → `SubmitTestFrameForWidget(widget, gen)`
5. `SubmitTestFrameForWidget` → `producer->CreateExportFrame()` → real DMAbuf export via `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA`
6. `NativeFrameToMojomFrame` converts `base::ScopedFD` fds → `mojo::PlatformHandle` fds
7. `gpu_host_remote_->SubmitFrame(mojom_frame, callback)` → Browser `QnxGpuHost::SubmitFrame`
8. Browser validates metadata/generation and calls `QnxFrameImporter::ImportAndDisplayFrame`

### 3. `qnx_gpu_service.cc` — `ResizeWidget` trigger

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc`

After producer recreation in `ResizeWidget`, added the same guard pattern:

```cpp
// ---- Phase 5: bounded SubmitFrame trigger on ResizeWidget ----
// After the producer is re-created at the new size, submit a test frame
// to validate the resized export path.  Same guard as AttachWidget.
if (enable_attach_test_frame_ && gpu_host_remote_ && producer->is_valid()) {
  DLOG(INFO) << "QnxGpuService::ResizeWidget: trigger: calling "
                "SubmitTestFrameForWidget(widget="
             << widget << ", generation=" << generation << ")";
  SubmitTestFrameForWidget(widget, generation);
} else if (enable_attach_test_frame_ && !gpu_host_remote_) {
  DLOG(WARNING) << "QnxGpuService::ResizeWidget: enable_attach_test_frame_ "
                   "is true but gpu_host_remote_ is null; skipping";
} else if (enable_attach_test_frame_ && !producer->is_valid()) {
  DLOG(WARNING) << "QnxGpuService::ResizeWidget: enable_attach_test_frame_ "
                   "is true but producer is not valid; skipping";
}
```

### 4. fd ownership / move semantics — no changes needed

The existing `NativeFrameToMojomFrame` implementation already correctly handles fd ownership:

```cpp
// Convert planes: base::ScopedFD → mojo::PlatformHandle.
// Mojo serialization uses SCM_RIGHTS fd passing; the PlatformHandle
// is serialized as a Mojo handle, and Mojo IPC internally calls dup(2)
// when passing to the remote process, so the local ScopedFD receives a dup
// and is safely closed when it goes out of scope at function return.
for (size_t i = 0; i < frame.planes.size(); ++i) {
  const QnxDmaBufPlane& native_plane = frame.planes[i];
  mojo::PlatformHandle handle;
  if (native_plane.fd.is_valid()) {
    base::ScopedFD tmp_fd(native_plane.fd.get());
    handle = mojo::PlatformHandle(std::move(tmp_fd));
  }
  qnx::QnxDmaBufPlanePtr mojom_plane(
      std::in_place,
      std::move(handle),
      native_plane.stride,
      native_plane.offset,
      native_plane.size);
  mojom_frame->planes.push_back(std::move(mojom_plane));
}
```

This pattern is already compile-safe: `ScopedFD::get()` returns the raw fd without transferring ownership, then `ScopedFD(tmp_fd.get())` creates a dup that the new `ScopedFD` owns, which is then moved into `PlatformHandle`. No fds are leaked or double-closed. No changes were needed.

## Trigger Behavior Summary

| Event | Producer state | `gpu_host_remote_` | Action |
|---|---|---|---|
| `AttachWidget` | Created + initialized | Bound | `SubmitTestFrameForWidget` → `QnxGpuHost::SubmitFrame` |
| `AttachWidget` | Created + initialized | **Not bound** | Log warning; skip |
| `AttachWidget` | Not valid | Any | Log warning; skip |
| `ResizeWidget` | Re-created + initialized | Bound | `SubmitTestFrameForWidget` → `QnxGpuHost::SubmitFrame` |
| `ResizeWidget` | Not valid | Any | Log warning; skip |
| `DetachWidget` | Removed | Any | No trigger |

**What `SubmitTestFrameForWidget` does end-to-end:**
1. Calls `QnxRenderProducer::CreateExportFrame()` which:
   - Creates a DRM EGLImage via `eglCreateDRMImageMESA` with ARGB32/Scanout/Share attributes
   - Queries DMAbuf metadata via `eglExportDMABUFImageQueryMESA`
   - Exports real DMAbuf fds via `eglExportDMABUFImageMESA`
   - Returns `QnxDmaBufFrame` with owned `base::ScopedFD` fds
2. Converts to mojom via `NativeFrameToMojomFrame` (ScopedFD → PlatformHandle)
3. Calls `gpu_host_remote_->SubmitFrame(mojom_frame, callback)`
4. Browser-side `QnxGpuHost::SubmitFrame`:
   - Validates metadata/generation/widget/GPU-attached state
   - Calls `QnxFrameImporter::ImportAndDisplayFrame` → EGL import → Screen display

## Validation

### Whitespace check
```bash
cd /home/yuta/chromium/src/cef
git diff --check patch/qnx/chromium/new_files/ui/ozone/platform/qnx/
```
Result: **passed** — no whitespace errors.

### Root-source temporary validation

```bash
# Apply Phase 3 GN patches
git apply -p0 patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
git apply -p0 patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch

# Copy CEF new_files to root source
mkdir -p ui/ozone/platform/qnx
cp -R patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/

# GN generation
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_submitframe_trigger \
  --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```
Result: `Done. Made 33099 targets from 4278 files in 2749ms`

```bash
# Ninja build
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_submitframe_trigger \
    ui/ozone/platform/qnx/mojom:mojom \
    ui/ozone/platform/qnx:qnx
```
Result: **798/798 targets compiled successfully.** No errors, no warnings.

Object files produced:
- `qnx_gpu_service.o` (95.5 KB) — includes new trigger logic
- `qnx_render_producer.o` (140.3 KB)
- `qnx_gpu_host.o` (98.6 KB)
- `qnx_frame_importer.o` (178.4 KB)
- `ozone_platform_qnx.o` (161.8 KB)

### Post-validation cleanup
- `ui/ozone/platform/qnx/` removed from root source tree.
- Root source confirmed clean (no diff in `ui/ozone/BUILD.gn`, `ui/ozone/platform/qnx/`).
- `out/qnx_phase5_submitframe_trigger/` retained (no source tree pollution).

## Compile Target Status

| Target | Status |
|--------|--------|
| `ui/ozone/platform/qnx/mojom:mojom` | ✅ pass |
| `ui/ozone/platform/qnx:qnx` (all 798 targets) | ✅ pass |

## Remaining Work

### QEMU virgl runtime smoke (Phase 5 or Phase 6)
- Requires `cefsimple --ozone-platform=qnx` run under QEMU virgl with no `--in-process-gpu`.
- At runtime, `AttachWidget` will fire `SubmitTestFrameForWidget` and `QnxGpuHost::SubmitFrame` will be called.
- `QnxFrameImporter::ImportAndDisplayFrame` attempts real EGL/Screen display.
- Expected runtime behavior: GPU logs `QnxGpuService::AttachWidget: trigger: calling SubmitTestFrameForWidget`, browser logs `QnxGpuHost::SubmitFrame: accepted=true` or deferred with scaffold diagnostic.
- Screenshot capture and visual verification needed.

### Robust multi-plane/modifier support
- Current scaffold defensively returns `false` for multi-plane frames (e.g., NV12).
- `QnxFrameImporter::BuildDmaBufAttrs` needs `EGL_EXT_image_dma_buf_import_modifiers` support.
- Real hardware validation required.

### Crash/restart runtime smoke (Phase 6)
- `QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach` increments widget generation on GPU disconnect.
- `QnxGpuHost::ReportProducerLost` updates widget record on GPU producer loss.
- Runtime crash/restart validation with `cefsimple --ozone-platform=qnx` pending.

### Full render pipeline wiring
- `QnxRenderProducer::CreateExportFrame()` creates a demo DRM image but does not copy GPU framebuffer content.
- Real pipeline requires GLES2 blit from render target to DRM EGLImage.

## Files Changed

| File | Change |
|------|--------|
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.h` | Added `enable_attach_test_frame_ = true` member |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc` | Added `SubmitTestFrameForWidget` trigger in `AttachWidget` and `ResizeWidget` after producer init, with guard/logging |
