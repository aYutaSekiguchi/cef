# QNX Ozone Phase 5 — Runtime AttachWidget + SubmitFrame path

**Date:** 2026-07-08
**Author:** Phase 5 smoke pass after a clean content_shell build
**Status:** AttachWidget → SubmitFrame → Browser SubmitFrame: ENTERED + VALIDATION_PASSED + about-to-ImportAndDisplayFrame all confirmed. Browser segfaults inside `QnxFrameImporter::ImportAndDisplayFrame`.

## Context

Phase 5 was previously accepted in the unit/scaffolding sense but had not been
exercised end-to-end in QEMU virgl because content_shell failed to build for
QNX. The new build pipeline (commit `83289e7b3`, see
`qnx-ozone-phase5-gpu-trace-logging-2026-07-04.md` for the trace switch and
follow-up commits) produces a working `content_shell`. The first QEMU virgl
smoke run printed only the `QnxGpuService::Initialize: gpu_host_remote bound`
trace, then stopped. This session traces the next three blockers and lands
fixes that take the trace chain all the way to
`QnxGpuHost::SubmitFrame: about to call ImportAndDisplayFrame`.

## What was working at the start of this session

- content_shell boots, browser process picks QNX ozone, Screen window is
  created (qnx_platform_event_source.cc VERBOSE1 logs visible).
- GPU process spawns (PID differs from browser; VizNullHypothesis is logged).
- `QnxGpuService::Initialize: gpu_host_remote bound; GPU process is ready to
  call SubmitFrame` is printed once. The Browser's
  `QnxGpuPlatformSupportHost::OnGpuServiceLaunched` clearly ran and called
  `gpu_service_remote_->Initialize(...)` successfully.
- `QnxGpuService::AttachWidget` is **never** printed. SubmitFrame never
  fires. The OOP path is idle.

## Three blockers, in the order they appeared

### Blocker 1 — `--ozone-qnx-gpu-trace` is not propagated to the GPU process

**Symptom:** `QNX_OZONE_GPU_TRACE` lines come exclusively from the Browser
process; the GPU process's `IsQnxGpuTraceEnabled()` returns false, so any
GPU-side `LOG(INFO)` is gated off.

**Root cause:** `content/browser/gpu/gpu_process_host.cc`'s `kSwitchNames[]`
copies a known set of upstream Chromium switches from the Browser's command
line to the GPU's. `kOzonePlatform` is in there, but the QNX-local
`kOzoneQnxGpuTraceSwitch` is not. The GPU process therefore sees no
`--ozone-qnx-gpu-trace` even when the Browser does.

**Fix:** add the literal `"ozone-qnx-gpu-trace"` to `kSwitchNames[]` under
the existing `#if BUILDFLAG(IS_OZONE)` block. This is the standard
Chromium pattern for platform-specific ozone switches (matches e.g.
`kOzoneDumpFile` two lines above). Patch:
`cef/patch/patches/qnx/chromium/gpu_process_host_qnx_trace_switch.patch`.

### Blocker 2 — `AttachWidget` is never sent to the GPU

**Symptom:** with the trace switch now propagating, the GPU process
emits `QnxGpuService::Initialize: gpu_host_remote bound` but still no
`AttachWidget` ever fires. SubmitFrame stays at zero.

**Root cause:** `QnxGpuPlatformSupportHost::AttachExistingWidgets(host_id)` is
the only call site for `gpu_control_remote_->AttachWidget(...)`. It runs
once, right at the end of `OnGpuServiceLaunched`, and iterates
`window_manager_->GetWidgetRecordsForTestingOrGpuAttach()`. For
`content_shell about:blank`, the Browser starts → GPU launches → AttachExistingWidgets
sees an empty record table → returns "no existing widgets". The actual
Screen window is then created later by `QnxWindow::QnxWindow()`, which
calls `manager_->AddWindow(this)`. The window manager's `AddWindow` adds
the record but never tells the support host about the new widget, so the
GPU process never learns that the widget exists.

**Fix:** introduce `QnxGpuPlatformSupportHost::AttachNewWidget(widget)` on
the support host side, give `QnxWindowManager` a non-owning
`gpu_platform_support_host_` pointer set from `OzonePlatformQnxImpl` after
both members are constructed, and have `AddWindow` forward
`AttachNewWidget(widget)` when the GPU service is already connected. The
existing `AttachExistingWidgets` pass continues to handle widgets that
existed before the GPU launch. Patch:
`cef/patch/patches/qnx/chromium/qnx_gpu_attach_new_widget_after_connect.patch`
(5 files: window_manager.{h,cc}, gpu_platform_support_host.{h,cc},
ozone_platform_qnx.cc).

### Blocker 3 — `AttachWidget` reaches the GPU with `size=0x0`

**Symptom:** after Blocker 2, the smoke now logs:

```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 size=0x0
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget (remote_bound=true producer_valid=true)
```

`QnxGpuService::SubmitTestFrameForWidget` is called next, but no
`QnxGpuService::SubmitTestFrameForWidget: ... calling SubmitFrame` trace
follows. The trace shows the path is taken, but `CreateExportFrame()`
silently fails (DLOG-only, so the failure cause is invisible) and the
function returns before calling `SubmitFrame`.

**Root cause:** `QnxWindowManager::AddWindow` defaulted `record.size` to
`gfx::Size()` (0x0). `QnxGpuPlatformSupportHost::AttachNewWidget` was
reading `record->size` and shipping `0x0` over Mojo. The GPU-side
`QnxRenderProducer` was being initialized with `size=0x0`, and
`eglCreateDRMImageMESA` on a 0x0 surface fails on QEMU virgl.

**Fix:** change the `AddWindow` signature from
`AddWindow(QnxWindow*)` to `AddWindow(QnxWindow*, const gfx::Size&)`,
store `record.size = initial_size`, and have `QnxWindow::QnxWindow` pass
`bounds.size()`. `QnxWindow::QnxWindow` is the only caller. Patch:
`cef/patch/patches/qnx/chromium/qnx_window_addwindow_initial_size.patch`
(3 files: window_manager.{h,cc}, qnx_window.cc).

After this fix, the trace shows the correct non-zero size:

```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 size=800x600
```

## Blockers 4 + 5 — full handshake reaches `ImportAndDisplayFrame` then segfaults

With blockers 1–3 fixed, the smoke now produces a complete handshake:

```
QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 size=800x600
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget: widget=1 generation=1 planes=1 size=800x600; calling gpu_host_remote_->SubmitFrame
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: ENTERED
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: VALIDATION_PASSED widget=1 generation=1; proceeding to EGL/Screen import
```

…but the Browser process then segfaults (exit 139 = SIGSEGV) inside
`QnxFrameImporter::ImportAndDisplayFrame`. The DLOG lines inside
`QnxFrameImporter` are suppressed in release builds, so we cannot tell
from logs alone whether the crash is in EGL display init, in
`GetOrCreateWindowState` (which dereferences `record->screen_win`), or
later in the import pipeline. To localize the crash for the next
session, a single grep-stable trace line was added immediately before
the `ImportAndDisplayFrame` call (gated by `--ozone-qnx-gpu-trace`):

```cpp
if (IsQnxGpuTraceEnabled()) {
  LOG(INFO) << "QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: about to "
               "call ImportAndDisplayFrame widget=" << frame->widget;
}
```

Patch: `cef/patch/patches/qnx/chromium/qnx_gpu_host_add_import_trace.patch`
(1 file: qnx_gpu_host.cc, 4-line addition).

After the next rebuild the trace appeared for the first time:

```
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: about to call ImportAndDisplayFrame widget=1
```

and the process segfaulted a few milliseconds later inside the import
pipeline. The most likely immediate cause: `QnxWidgetRecord::screen_win`
is still nullptr, because `QnxWindow::CreateScreenWindow` allocates
`screen_win_` locally but never calls
`manager_->SetScreenWindow(widget_, screen_win_)`. `QnxFrameImporter`
returns "scaffold: no screen_window_t for widget; import/display
deferred" (a soft failure that should not crash, so the segfault is
likely a later EGL call dereferencing something that wasn't set up).

## Status

Phase 5 is functionally complete on the GPU-side
`SubmitFrame` / `NativeFrameToMojomFrame` path:

- Browser-owned `QnxGpuHost` is bound on the GPU side.
- `QnxGpuPlatformSupportHost::OnGpuServiceLaunched` fires for both
  existing widgets (initial launch) and new widgets (AttachNewWidget
  after AddWindow).
- `QnxGpuService::Initialize` runs with a non-zero size.
- `QnxGpuService::AttachWidget` is delivered to the GPU.
- `QnxRenderProducer::CreateExportFrame` returns a 1-plane
  800x600 AR24 frame.
- `QnxGpuService::SubmitTestFrameForWidget` calls
  `gpu_host_remote_->SubmitFrame(std::move(mojom_frame), ...)`.
- `QnxGpuHost::SubmitFrame: ENTERED` on the Browser side.
- `QnxGpuHost::SubmitFrame: VALIDATION_PASSED` on the Browser side.
- The first `QnxFrameImporter::ImportAndDisplayFrame` call begins.

The double-close bug in `NativeFrameToMojomFrame` (mojo `PlatformHandle`
ownership) was also fixed in this session as part of blocker 4:
patch `cef/patch/patches/qnx/chromium/qnx_gpu_service_native_frame_ownership.patch`.

All five patches are committed at `53f8cd73a QNX: notify GPU of widgets
created after AttachExistingWidgets ran` (Cumulative: 13 files changed,
681 insertions(+), 92 deletions(-)).

## Open items (next session)

1. **Resolve Browser segfault in `ImportAndDisplayFrame`.** Most likely:
   `QnxWindow::CreateScreenWindow` must call
   `manager_->SetScreenWindow(widget_, screen_win_)` after a successful
   `screen_create_window`. A subsequent smoke with
   `--ozone-qnx-gpu-trace` should now show the trace moving past
   `about to call ImportAndDisplayFrame` and ideally a `FINAL accepted=...`
   trace on completion (or a specific deferred diagnostic).
2. **Add trace inside `QnxFrameImporter::ImportAndDisplayFrame`** if the
   `SetScreenWindow` fix alone does not get the smoke to a `FINAL` trace.
   Likely places: EGL display init result, `GetOrCreateWindowState`
   result, per-plane `eglCreateImageKHR` result, and the final
   `eglSwapBuffers` result.
3. **Mark Phase 5 acceptance items completed** in
   `docs/qnx/ozone-out-of-process-gpu-plan.md` once `FINAL accepted=true
   display_ok=true; eglSwapBuffers reached` is observed, then proceed to
   Phase 6 (Browser/GPU reconnect and crash recovery) per the existing
   plan.

## Validation commands (for the next session)

```bash
cd /home/yuta/chromium/src
# Bootstrap (idempotent if already at clean baseline)
QNX_SDP_ROOT=/home/yuta/qnx800 \
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  timeout 1800 /home/yuta/chromium/src/cef/tools/cef_create_projects_qnx.sh \
  --build-type Release --qnx-sdp-root /home/yuta/qnx800

# Build content_shell
source out/qnx_release/qnx_env.sh
ninja -C out/qnx_release -j10 content/shell:content_shell

# QEMU virgl smoke
/home/yuta/chromium/src/cef/tools/qnx_run.sh --virgl --kill-existing \
  --timeout 60 -- \
  "./content_shell --ozone-platform=qnx --use-gl=egl --no-sandbox \
   --ozone-qnx-gpu-trace --enable-logging=stderr --v=1 about:blank 2>&1" \
  > out/qnx_release/content_shell_qnx_phase5_followup.log 2>&1
grep 'QNX_OZONE_GPU_TRACE\|FATAL\|Check failed' \
  out/qnx_release/content_shell_qnx_phase5_followup.log
```

Expected successful log sequence:

```
QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 size=800x600
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget: widget=1 generation=1 planes=1 size=800x600; calling gpu_host_remote_->SubmitFrame
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: ENTERED
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: VALIDATION_PASSED widget=1 generation=1; proceeding to EGL/Screen import
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: about to call ImportAndDisplayFrame widget=1
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: FINAL widget=1 generation=1 accepted=true display_ok=true; eglSwapBuffers reached
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget callback: widget=1 generation=1 accepted=true diagnostic=
```
