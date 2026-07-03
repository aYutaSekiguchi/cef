# QNX Ozone Phase 5 Demo Render Surface — 2026-07-03

## Scope

Fix the next bounded runtime smoke blocker after the QNX Ozone keyboard-layout startup crash was resolved. The demo reached:

```text
[WARNING:ui/ozone/demo/window_manager.cc:45] No display delegate; falling back to test window
[ERROR:ui/ozone/demo/software_renderer.cc:44] Failed to create software surface
[ERROR:ui/ozone/demo/demo_window.cc:100] Failed to initialize renderer.
```

The goal was to provide a compile-safe and runtime-usable `SurfaceOzoneCanvas` path for `ui/ozone/demo:ozone_demo`. This is still a bounded Ozone demo smoke aid, not final CEF out-of-process GPU acceptance.

## Files changed

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_ozone_canvas.h` — new `SurfaceOzoneCanvas` implementation declaration.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_ozone_canvas.cc` — new software canvas implementation.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_factory.h` — declares `CreateCanvasForWidget()` and `CreateForGpu()`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_factory.cc` — wires `CreateCanvasForWidget()` to `QnxSurfaceOzoneCanvas`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.{cc,h}` — exposes a process-local singleton lookup used by the canvas to find the browser-local widget record.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc` — creates a browser-side `QnxSurfaceFactoryOzone` during `InitializeUI()` so `ozone_demo` can call `CreateCanvasForWidget()`; GPU side still creates its factory in `InitializeGPU()`.
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn` — adds the new canvas files.

## Implementation summary

`QnxSurfaceOzoneCanvas` implements Chromium's software rendering interface:

- `ResizeCanvas()` allocates a CPU-backed Skia raster surface.
- `GetCanvas()` returns the Skia canvas for `SoftwareRenderer`.
- `PresentCanvas()` copies pixels row-by-row into the current QNX Screen render buffer and calls `screen_post_window()`.
- `CreateVSyncProvider()` returns `nullptr`; the demo uses its timer fallback.

`QnxSurfaceFactoryOzone::CreateCanvasForWidget(widget)` now returns a `QnxSurfaceOzoneCanvas`. The canvas looks up `screen_window_t` from `QnxWindowManager::GetInstance()` using the stable `gfx::AcceleratedWidget` record. The raw `screen_window_t` stays browser-process local and is not serialized.

## Validation

### Build

The root tree was temporarily patched/copied with the CEF-managed QNX Ozone files, then rebuilt:

```bash
cd /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_runtime_fix ui/ozone/demo:ozone_demo -k 20
```

Result:

```text
[843/843] LINK ./ozone_demo
```

`git diff --check` over touched CEF-managed files passed.

### Runtime smoke

Run:

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime_fix \
  ./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 45 -- \
  ./ozone_demo --ozone-platform=qnx --window-size=320x240 \
  2>&1 | tee out/qnx_phase5_runtime_fix/ozone_demo.qnx.canvas.log
```

Result:

- No `Received signal 11`.
- No `segmentation violation`.
- No `Failed to create software surface`.
- The command timed out after 45s because `ozone_demo` keeps running.

The remaining GL errors are expected for this bounded smoke because the demo falls back to the new software canvas path:

```text
[ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
[ERROR:ui/ozone/common/gl_ozone_egl.cc:26] GLDisplayEGL::Initialize failed.
[WARNING:ui/ozone/demo/window_manager.cc:45] No display delegate; falling back to test window
TimeoutError: QNX command timed out after 45.0 seconds
```

### Screenshot capture

Run:

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime_fix \
  ./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 90 -- sh -c \
  './ozone_demo --ozone-platform=qnx --window-size=320x240 >/tmp/qnx_ozone_demo.out 2>&1 & \
   pid=$!; sleep 8; \
   screenshot -file=/mnt/nfs/out/qnx_phase5_runtime_fix/qnx-ozone-demo.bmp -verbose; \
   rc=$?; kill $pid 2>/dev/null || true; wait $pid 2>/dev/null || true; \
   echo SCREENSHOT_RC:$rc; cat /tmp/qnx_ozone_demo.out'
```

Result:

```text
Captured region of size 1280 by 768 and starting at (0, 0) is saved in /mnt/nfs/out/qnx_phase5_runtime_fix/qnx-ozone-demo.bmp
SCREENSHOT_RC:0
[WARNING:ui/ozone/demo/window_manager.cc:45] No display delegate; falling back to test window
__PI_QNX_EXIT__:0
```

Host-side artifact:

- `out/qnx_phase5_runtime_fix/qnx-ozone-demo.bmp`

Host file inspection:

```text
PC bitmap, Windows 95/NT4 and newer format, 1280 x -768 x 32, cbSize 3932282
sample_unique=352
```

The image analysis tool was unavailable due authentication, so the screenshot is recorded as a captured artifact rather than visually attested here.

## Cleanup

After validation:

```bash
git checkout -- build/config/ozone.gni ui/ozone/BUILD.gn ui/ozone/public/ozone_platform.cc
rm -rf ui/ozone/platform/qnx
```

Root temporary source state is clean again. Build/log/screenshot artifacts remain in `out/qnx_phase5_runtime_fix/`.

## Result

The `Failed to create software surface` blocker is resolved for the bounded `ozone_demo` smoke. QNX Ozone can now start the demo, create a Screen-backed software canvas, run until timeout, and capture a QNX screenshot without using `--in-process-gpu`.

## Remaining work

- This is a single-process `ozone_demo` software-canvas smoke, not the final out-of-process CEF GPU path.
- Need runtime proof that the Phase 5 Mojo path actually calls `QnxGpuHost::SubmitFrame` in an out-of-process browser/GPU run.
- Need robust visual verification after browser-side DMAbuf import/display is exercised.
- Need crash/restart validation for the out-of-process GPU producer path.
