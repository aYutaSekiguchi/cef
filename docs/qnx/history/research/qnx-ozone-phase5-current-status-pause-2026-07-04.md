# QNX Ozone Phase 5 current status / stability pause — 2026-07-04

## Why this note exists

During the attempt to move from the bounded `ozone_demo` smoke to an out-of-process Chromium smoke, the machine became unstable while large Chromium builds were running. Work is paused and this note records the current state so the next session can resume safely without relying on chat history.

## Safety status at pause

- No active heavy build/runtime processes were found:
  - checked for `ninja`, `clang`, `ld.lld`, `qemu-system`, `qnx_phase5_oop_smoke`, and `content_shell` processes.
- Temporary root Chromium source state has been cleaned:
  - `build/config/ozone.gni` clean
  - `ui/ozone/BUILD.gn` clean
  - `ui/ozone/public/ozone_platform.cc` clean
  - temporary `ui/ozone/platform/qnx/` removed
- Durable source of truth remains under CEF-managed paths:
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`
  - `docs/qnx/`

## Completed since the previous runtime smoke blocker

### 1. Startup crash fixed

Report:

- `docs/qnx/history/research/qnx-ozone-phase5-runtime-startup-crash-fix-2026-07-03.md`

Root cause:

- QNX Ozone did not install a `KeyboardLayoutEngine` during `InitializeUI()`.
- `ozone_demo` dereferenced the missing engine after Ozone UI initialization.

Durable fix:

- QNX Ozone installs `StubKeyboardLayoutEngine`, following the pattern used by other Ozone backends that do not yet provide a real layout engine.
- Screen error logging was also made safer to avoid null/unsafe `PLOG`-style paths while debugging runtime startup.

Catalog:

- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ozone-demo-keyboard-layout-engine.md`

### 2. Bounded software-canvas demo smoke completed

Report:

- `docs/qnx/history/research/qnx-ozone-phase5-demo-render-surface-2026-07-03.md`

Durable implementation:

- Added `QnxSurfaceOzoneCanvas` under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`.
- `QnxSurfaceFactoryOzone::CreateCanvasForWidget()` now returns the QNX software canvas.
- The canvas uses a Skia raster surface and posts pixels to the browser-local QNX Screen window with `screen_post_window()`.
- `screen_window_t` remains process-local; the canvas obtains it through the browser-side `QnxWindowManager` record for the stable `gfx::AcceleratedWidget`.

Validation summary:

```text
ninja -C out/qnx_phase5_runtime_fix ui/ozone/demo:ozone_demo
[843/843] LINK ./ozone_demo
```

Runtime:

```text
./ozone_demo --ozone-platform=qnx --window-size=320x240
```

Observed:

- No exit 139.
- No `Failed to create software surface`.
- Remaining EGL initialization errors are expected for this bounded smoke because the demo falls back to software rendering.
- The process times out/runs because `ozone_demo` remains alive.
- Screenshot capture succeeded:
  - `out/qnx_phase5_runtime_fix/qnx-ozone-demo.bmp`

## Incomplete / not accepted

### Out-of-process `SubmitFrame` smoke is not complete

The next intended milestone was an out-of-process Chromium/CEF runtime smoke that exercises:

```text
Browser QnxGpuHost <-> GPU QnxGpuService <-> QnxRenderProducer -> SubmitFrame
```

This is still incomplete.

### `content_shell` build attempt was too broad and stopped at a non-QNX-Ozone test-support compile error

An exploratory `content/shell:content_shell` build was started in:

- `out/qnx_phase5_oop_smoke/`

It progressed substantially but did not produce `content_shell`. Logs left behind:

- `out/qnx_phase5_oop_smoke/content_shell.build.log`
- `out/qnx_phase5_oop_smoke/content_shell.resume.log`
- `out/qnx_phase5_oop_smoke/content_shell.resume2.log`

The latest observed first actionable failure was outside the QNX Ozone backend:

```text
FAILED: obj/content/test/test_support/mock_navigation_throttle_registry.o
../../content/public/test/mock_navigation_throttle_registry.h:52:66: error:
  non-virtual member function marked 'override' hides virtual member function
../../content/public/browser/navigation_throttle_registry.h:30:16: note:
  hidden overloaded virtual function 'content::NavigationThrottleRegistry::AddThrottle'
  declared here: different number of parameters (2 vs 1)
```

This was not investigated or fixed because the user reported machine instability during the heavy build. Treat this build as an abandoned probe, not accepted validation.

## Current blockers

1. Need a smaller out-of-process GPU smoke than full `content_shell` or `cefsimple`, or explicit user approval before running another broad build.
2. Need runtime evidence that `QnxGpuService` is launched by the GPU process and that `QnxGpuHost::SubmitFrame` is reached through Mojo.
3. Need final DMAbuf import/display visual proof for the out-of-process path, separate from the single-process software-canvas `ozone_demo` proof.

## Recommended safe next steps

Do not immediately restart a broad Chromium build. Instead:

1. Do a read-only target/entrypoint audit for the smallest executable that launches a GPU process and initializes Ozone.
2. Prefer `gn desc`, `ninja -t targets`, and source inspection over full compilation.
3. If a build is needed, ask for user approval and run with a constrained parallelism, for example:

```bash
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_oop_smoke -j2 <target>
```

4. Keep runtime tests bounded and use `cef/tools/qnx_run.sh --virgl --kill-existing --timeout <short>`.
5. Continue to clean temporary root Chromium patch state after every validation pass.

## Resume pointer

The durable plan has been updated at:

- `docs/qnx/ozone-out-of-process-gpu-plan.md`

Resume from: **find a smaller/safer out-of-process GPU smoke target, or ask before launching another broad `content_shell` / `cefsimple` build.**
