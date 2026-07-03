# QNX Ozone Phase 5 Runtime Startup Crash Fix — 2026-07-03

## Scope

Continue the first QNX Ozone runtime smoke after `ozone_demo --ozone-platform=qnx` exited 139 under QEMU virgl. Keep the fix QNX-local and bounded; do not use `--in-process-gpu` as acceptance.

## Initial failure

From `docs/qnx/history/research/qnx-ozone-phase5-runtime-smoke-attempt-2026-07-03.md`:

```text
Received signal 11 si_addr=0x0
Symbol: /mnt/nfs/out/qnx_phase5_runtime/./ozone_demo _ZNSt3__216__pad_and_outputB7v160006IcNS_11char_traitsIcEEEENS_19ostreambuf_iteratorIT_T0_EES6_PKS4_S8_S8_RNS_8ios_baseES4_ + 0x0000000002ea
segmentation violation     (core dumped) sh -c './ozone_demo --ozone-platform=qnx --window-size=320x240'
__PI_QNX_EXIT__:139
```

`--help` exited 0, and `--ozone-platform=headless` timed out without segfaulting.

## Investigation

A timed-out worker left the hypothesis that a nullable C string was being streamed through logging. I first made QNX Screen error logging null-safe in `QnxScreenContext` and `QnxPlatformEventSource`, but the crash persisted.

Temporary breadcrumbs around `OzonePlatformQnxImpl::InitializeUI()` showed the crash happened **after** QNX `InitializeUI()` returned successfully and before `InitializeGPU()`:

```text
[qnx-ozone] InitializeUI: start
[qnx-ozone] QnxScreenContext: creating screen context
[qnx-ozone] QnxScreenContext: screen context created
[qnx-ozone] InitializeUI: screen_context valid=1
[qnx-ozone] InitializeUI: window_manager ready
[qnx-ozone] InitializeUI: event_source constructed
[qnx-ozone] InitializeUI: event_source started
[qnx-ozone] InitializeUI: success
Received signal 11 si_addr=0x0
```

A second temporary breadcrumb in `ui/ozone/public/ozone_platform.cc` proved `DeviceDataManager::CreateInstance()` was not the crash point:

```text
[ozone-debug] InitializeForUI: after platform InitializeUI
[ozone-debug] InitializeForUI: before DeviceDataManager
[ozone-debug] InitializeForUI: after DeviceDataManager
Received signal 11 si_addr=0x0
```

The next statement in `ui/ozone/demo/ozone_demo.cc` is:

```cpp
ui::KeyboardLayoutEngineManager::GetKeyboardLayoutEngine()
    ->SetCurrentLayoutByName("us", base::DoNothing());
```

Headless, Wayland, and X11 Ozone platforms create a `StubKeyboardLayoutEngine` during `InitializeUI()`. QNX did not, so the demo dereferenced a null keyboard layout engine.

## Fix

QNX `InitializeUI()` now mirrors the other Ozone platforms:

- include `ui/events/ozone/layout/keyboard_layout_engine_manager.h`
- include `ui/events/ozone/layout/stub/stub_keyboard_layout_engine.h`
- create `std::unique_ptr<StubKeyboardLayoutEngine> keyboard_layout_engine_`
- call `KeyboardLayoutEngineManager::SetKeyboardLayoutEngine(keyboard_layout_engine_.get())`

I kept the null-safe QNX Screen error logging changes because they remove a real future footgun when QNX Screen APIs return an error string pointer as null.

## Files changed

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen_context.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.cc`

Temporary breadcrumbs in root `ui/ozone/public/ozone_platform.cc` and QNX startup code were removed before the final state.

## Validation

Rebuilt the bounded visual target after applying/copying QNX managed files into a temporary root tree:

```bash
cd /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_runtime_fix ui/ozone/demo:ozone_demo -k 20
```

Result:

```text
[842/842] LINK ./ozone_demo
```

Runtime:

```bash
BUILD_DIR=/home/yuta/chromium/src/out/qnx_phase5_runtime_fix \
  ./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 45 -- \
  ./ozone_demo --ozone-platform=qnx --window-size=320x240 \
  2>&1 | tee out/qnx_phase5_runtime_fix/ozone_demo.qnx.final.log
```

Result: no `Received signal 11`, no `segmentation violation`, no exit 139. The command reached the 45s timeout because `ozone_demo` keeps running.

Current runtime output ends at the next blocker:

```text
[ERROR:ui/gl/gl_display.cc:673] Initialization of all EGL display types failed.
[ERROR:ui/ozone/common/gl_ozone_egl.cc:26] GLDisplayEGL::Initialize failed.
[WARNING:ui/ozone/demo/window_manager.cc:45] No display delegate; falling back to test window
[ERROR:ui/ozone/demo/software_renderer.cc:44] Failed to create software surface
[ERROR:ui/ozone/demo/demo_window.cc:100] Failed to initialize renderer.
TimeoutError: QNX command timed out after 45.0 seconds
```

Cleanup:

```bash
git checkout -- build/config/ozone.gni ui/ozone/BUILD.gn ui/ozone/public/ozone_platform.cc
rm -rf ui/ozone/platform/qnx
```

Root temporary GN/source state is clean again.

## Result

The startup segfault is fixed. `ozone_demo --ozone-platform=qnx` now reaches the next runtime blocker instead of crashing.

## Remaining work

- Implement a QNX Ozone renderer surface path for `ozone_demo`/runtime smoke:
  - either `CreateCanvasForWidget()` software Screen surface, or
  - a real QNX window `GLSurface` path for `QnxGLOzoneEGL::CreateViewGLSurface()`.
- Then re-run QEMU virgl smoke and capture visual/log evidence.
- Full CEF out-of-process GPU smoke remains pending; `ozone_demo` is only a bounded Ozone startup probe.
