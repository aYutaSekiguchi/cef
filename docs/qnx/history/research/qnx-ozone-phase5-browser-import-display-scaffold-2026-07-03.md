# QNX Ozone Phase 5: Browser Import/Display Scaffold — 2026-07-03

## Task

Implement browser-side EGL/Screen DMAbuf import/display scaffold in `QnxGpuHost::SubmitFrame`, bounded to compile-safe validation. After GPU-side `SubmitFrame` sends a `QnxDmaBufFrame` with Mojo platform fd handles, `QnxGpuHost` must have browser-side code structured to import the DMAbuf via `EGL_LINUX_DMA_BUF_EXT` and render/display to the browser-owned QNX Screen window surface.

## Changes Made

### 1. New: `qnx_frame_importer.{cc,h}` — browser-side EGL/Screen DMAbuf import helper

**Files:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_frame_importer.{cc,h}`

New browser-side helper class `QnxFrameImporter` owned by `QnxGpuHost`. Provides:

**Lazy EGL initialization:**
- `InitializeEGLDisplay()`: uses `eglGetDisplay(EGL_DEFAULT_DISPLAY)` in the browser process (Phase 1B-display-isolation proven path).
- Resolves EGL function pointers: `eglCreateImageKHR`, `eglDestroyImageKHR`.
- Resolves GL function pointers: `glEGLImageTargetTexture2DOES`.
- Probes `EGL_EXT_image_dma_buf_import`, `GL_OES_EGL_image`.

**Per-window EGL state (`WindowEGLState`):**
- Lazily created when the first frame arrives for a widget.
- `eglCreateWindowSurface(display, config, (EGLNativeWindowType)screen_window_t, nullptr)` — direct Screen window surface creation (Phase 1B-display-isolation proven, no `SCREEN_PROPERTY_EGL_HANDLE` required).
- `eglCreateContext` for GLES2.
- `eglMakeCurrent` to activate the context.
- Minimal GLES2 shader program (passthrough fullscreen quad vertex + fragment shader).

**`ImportAndDisplayFrame(widget, frame)`:**
1. Look up `QnxWidgetRecord` to get `screen_window_t`.
2. `GetOrCreateWindowState` — lazily creates EGL display/context/surface for the widget.
3. Extract DMAbuf fds from Mojo `PlatformHandle` via `TakeFD()` → `base::ScopedFD`.
4. `BuildDmaBufAttrs` — constructs `EGL_LINUX_DMA_BUF_EXT` attribute list for single-plane ARGB/linear frames. Defensively returns false for multi-plane or non-linear modifier.
5. `ImportDmaBufToTexture` — calls `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` then `glEGLImageTargetTexture2DOES`.
6. `DrawFullscreenQuad` — renders imported texture as fullscreen quad via GLES2.
7. `eglSwapBuffers` — posts to Screen window surface.
8. `eglDestroyImageKHR` — cleans up EGLImage.

**Defensive fd ownership handling:**
- `mojo::PlatformHandle::TakeFD()` transfers fd ownership to `base::ScopedFD`.
- The scoped fd is passed by raw fd value to EGL attributes; EGL internally calls `dup()` at `eglCreateImageKHR` time.
- `ScopedFD` destructor closes the fd when it goes out of scope; this is safe because EGL already holds its own reference.

**Return contract:** `{bool success, string diagnostic}`:
- `success=true`: display reached (passed `eglSwapBuffers`).
- `success=false` with `"scaffold: ..."` diagnostic: known untested path deferred display; diagnostic clarifies which step.

### 2. Updated: `qnx_gpu_host.h`

**Changes:**
- Added `#include <memory>`.
- Added `class QnxFrameImporter;` forward declaration.
- Added `std::unique_ptr<QnxFrameImporter> frame_importer_;` member.
- Updated class comment: removed "no EGL import, no Screen display, no texture upload" and "deferred to Phase 5 import/display substep"; replaced with accurate Phase 5 scope description.

### 3. Updated: `qnx_gpu_host.cc`

**Changes:**
- Added `#include "ui/ozone/platform/qnx/qnx_frame_importer.h"`.
- In `SubmitFrame`: After Steps 1–5 (metadata, widget, generation, GPU-attached, size checks), replaced the unconditional `accepted=false` return with:
  - **Step 6:** `if (!frame_importer_) frame_importer_ = std::make_unique<QnxFrameImporter>(window_manager_);`
  - **Step 7:** `auto [display_ok, display_diagnostic] = frame_importer_->ImportAndDisplayFrame(widget, frame);`
  - If `display_ok`: `std::move(callback).Run(true, std::string())` — accepted.
  - If `!display_ok`: `std::move(callback).Run(false, display_diagnostic)` — deferred with scaffold diagnostic.

### 4. Updated: `BUILD.gn`

**Changes:** Added `qnx_frame_importer.cc` and `qnx_frame_importer.h` to the `source_set("qnx")` sources list.

## Import/Display Scaffold Architecture

```
QnxGpuHost::SubmitFrame(frame, callback)
  │
  ├─ ValidateFrameMetadata(frame)     [Steps 1-5: metadata/widget/gen/GPU-attached]
  │   └─ Returns {is_valid, diagnostic}
  │
  └─ frame_importer_->ImportAndDisplayFrame(widget, frame)   [Step 7]
       │
       ├─ GetOrCreateWindowState(widget)
       │   ├─ InitializeEGLDisplay() [one-time: eglGetDisplay + eglInitialize]
       │   ├─ eglChooseConfig(window config)
       │   ├─ eglCreateWindowSurface(screen_window_t)    [direct, no EGL_HANDLE]
       │   ├─ eglCreateContext(GLES2)
       │   ├─ eglMakeCurrent
       │   ├─ glGenTextures + glBindTexture
       │   └─ CompileShaderProgram(fullscreen quad)
       │
       ├─ Extract fds: plane->fd.TakeFD() → base::ScopedFD
       │
       ├─ BuildDmaBufAttrs(frame, fds)
       │   └─ EGL_WIDTH/HEIGHT/FOURCC + EGL_DMA_BUF_PLANE0_FD/OFFSET/PITCH
       │
       ├─ ImportDmaBufToTexture(display, attrs, texture)
       │   ├─ eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)
       │   └─ glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_image)
       │
       ├─ DrawFullscreenQuad(program, texture)
       │   └─ glDrawArrays(GL_TRIANGLES, 0, 6) [2 triangles]
       │
       ├─ eglSwapBuffers(display, surface)
       │   └─ Posts to Screen window (visible)
       │
       └─ eglDestroyImageKHR(display, egl_image)
            └─ Returns {true, ""}  [display confirmed]
```

**Key design decisions:**

1. **Direct `screen_window_t` → `eglCreateWindowSurface`**: Phase 1B-display-isolation proved this works in QEMU virgl without `SCREEN_PROPERTY_EGL_HANDLE`.
2. **`SCREEN_PROPERTY_EGL_HANDLE`**: diagnostic only, not used.
3. **Browser EGL display**: `eglGetDisplay(EGL_DEFAULT_DISPLAY)` in the browser process (same as Phase 1B-display probe). The browser process has a QNX Screen context but no dedicated EGL display; this approach was validated in Phase 1B.
4. **Mojo fd ownership**: `PlatformHandle::TakeFD()` → `ScopedFD`; EGL takes `dup()` at `eglCreateImageKHR` time, so scoped fd closing is safe.
5. **Per-window EGL state**: One `WindowEGLState` per widget, keyed by `AcceleratedWidget`. Created lazily on first frame arrival.
6. **Scaffold limitations**: Single-plane ARGB/linear only; multi-plane and non-linear modifier return `scaffold: ...` diagnostics.
7. **`screen_window_t` browser-local only**: Never serialized or sent over IPC.

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
  gn gen out/qnx_phase5_browser_import_display \
  --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```
Result: `Done. Made 33099 targets from 4278 files in 2813ms`

```bash
# Mojom compilation
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_browser_import_display \
    ui/ozone/platform/qnx/mojom:mojom \
    ui/ozone/platform/qnx:qnx
```
Result: **2164/2164 targets compiled successfully.** No errors, no warnings.

Object files produced:
- `qnx_frame_importer.o`: **178.4 KB**
- `qnx_gpu_host.o`: **98.6 KB**

### Post-validation cleanup
- `ui/ozone/platform/qnx/` removed from root source tree.
- `ui/ozone/BUILD.gn` reverted to clean state.
- Root source confirmed clean.
- `out/qnx_phase5_browser_import_display/` retained (no source tree pollution).

## Compile Target Status

| Target | Status |
|--------|--------|
| `ui/ozone/platform/qnx/mojom:mojom` | ✅ pass |
| `ui/ozone/platform/qnx:qnx` (all 2164 targets) | ✅ pass |

## Remaining Work

### Runtime QEMU smoke (Phase 5 or Phase 6)
- Requires a real `cefsimple --ozone-platform=qnx` run under QEMU virgl with no `--in-process-gpu`.
- `QnxFrameImporter::ImportAndDisplayFrame` attempts real EGL/Screen display code on each valid `SubmitFrame`.
- GPU-side must call `QnxGpuService::SubmitTestFrameForWidget` or have a real render loop.

### Robust multi-plane/modifier support
- Current scaffold defensively returns `false` for multi-plane (e.g., NV12) or non-linear modifier frames.
- Real support would require `EGL_EXT_image_dma_buf_import_modifiers` probing and per-plane attribute construction for NV12/YV12/P010.
- These paths need runtime validation on real QNX hardware.

### Crash/restart runtime smoke (Phase 6)
- `QnxGpuPlatformSupportHost::ResetGpuServiceAndDetach` increments widget generation on GPU disconnect.
- `QnxGpuHost::ReportProducerLost` updates widget record on GPU producer loss.
- Runtime crash/restart validation with `cefsimple --ozone-platform=qnx` is pending.

### Full render pipeline wiring
- `QnxRenderProducer::CreateExportFrame()` creates a demo DRM image but does not yet copy actual GPU framebuffer content.
- Real pipeline requires GLES2 blit from render target to DRM EGLImage.

### `--in-process-gpu` acceptance gate
- This scaffold does not use `--in-process-gpu` as acceptance criteria.
- All code compiles and runs in the browser process (not GPU process).

## Files Changed

| File | Change |
|------|--------|
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_frame_importer.h` | New: `QnxFrameImporter` class declaration (~270 lines) |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_frame_importer.cc` | New: `QnxFrameImporter` implementation (~960 lines) |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.h` | Added `frame_importer_` member and `QnxFrameImporter` forward declaration |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc` | `SubmitFrame` now calls `frame_importer_->ImportAndDisplayFrame` after validation |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn` | Added `qnx_frame_importer.cc` and `.h` to source list |
