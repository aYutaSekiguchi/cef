# QNX Ozone Phase 5 GPU Trace Logging — 2026-07-04

## Scope

Implement the user-approved diagnostic-logging-first step before a heavy `content_shell` build. No build, no QEMU run, no root Chromium edits. All changes are under CEF-managed QNX files.

## Files changed

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc`
- `docs/qnx/ozone-out-of-process-gpu-plan.md`

## Switch design

**Switch:** `--ozone-qnx-gpu-trace`

**Implementation:** local anonymous-namespace helper in each `.cc`:

```cpp
namespace {
constexpr char kOzoneQnxGpuTraceSwitch[] = "ozone-qnx-gpu-trace";
bool IsQnxGpuTraceEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(
      kOzoneQnxGpuTraceSwitch);
}
}  // namespace
```

Requires `#include "base/command_line.h"` in both files.

When the switch is absent, `IsQnxGpuTraceEnabled()` returns `false` and zero trace lines are emitted. Existing behavior is unchanged.

## Trace lines emitted (all use `QNX_OZONE_GPU_TRACE` prefix)

### GPU process — qnx_gpu_service.cc

**1. `QnxGpuService::Initialize` — after `gpu_host_remote_` binds:**
```
QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound; GPU process is ready to call SubmitFrame
```
(GPU process only)

**2. `QnxGpuService::AttachWidget` — entry:**
```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=N generation=G size=WxH
```
(GPU process only)

**3a. `QnxGpuService::AttachWidget` — trigger (remote_bound=true, producer_valid=true):**
```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=N generation=G TRIGGER SubmitTestFrameForWidget (remote_bound=true producer_valid=true)
```

**3b. `QnxGpuService::AttachWidget` — skip (remote_bound=false):**
```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=N generation=G SKIP SubmitTestFrameForWidget (remote_bound=false)
```

**3c. `QnxGpuService::AttachWidget` — skip (producer_valid=false):**
```
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=N generation=G SKIP SubmitTestFrameForWidget (producer_valid=false)
```
(Same three variants for `ResizeWidget`.)

**4a. `QnxGpuService::SubmitTestFrameForWidget` — before `SubmitFrame` call:**
```
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget: widget=N generation=G planes=P size=WxH; calling gpu_host_remote_->SubmitFrame
```

**4b. `QnxGpuService::SubmitTestFrameForWidget` — callback (accepted=true):**
```
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget callback: widget=N generation=G accepted=true diagnostic=
```

**4c. `QnxGpuService::SubmitTestFrameForWidget` — callback (accepted=false):**
```
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget callback: widget=N generation=G accepted=false diagnostic=<reason>
```

### Browser process — qnx_gpu_host.cc

**5. `QnxGpuHost::SubmitFrame` — entered:**
```
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: ENTERED
```

**6. `QnxGpuHost::SubmitFrame` — validation passed (widget exists, generation matches, GPU attached):**
```
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: VALIDATION_PASSED widget=N generation=G; proceeding to EGL/Screen import
```

**7a. `QnxGpuHost::SubmitFrame` — final (display ok, eglSwapBuffers reached):**
```
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: FINAL widget=N generation=G accepted=true display_ok=true; eglSwapBuffers reached
```

**7b. `QnxGpuHost::SubmitFrame` — final (deferred):**
```
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: FINAL widget=N generation=G accepted=false deferred=<diagnostic>
```

## Grep command to extract trace evidence

```bash
grep -E "QNX_OZONE_GPU_TRACE" <runtime_log_file>
```

Expected successful OOP path sequence:

```
QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1 TRIGGER SubmitTestFrameForWidget
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget: widget=1 generation=1; calling gpu_host_remote_->SubmitFrame
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: ENTERED
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: VALIDATION_PASSED widget=1 generation=1; proceeding to EGL/Screen import
QNX_OZONE_GPU_TRACE QnxGpuHost::SubmitFrame: FINAL widget=1 generation=1 accepted=true display_ok=true; eglSwapBuffers reached
QNX_OZONE_GPU_TRACE QnxGpuService::SubmitTestFrameForWidget callback: widget=1 generation=1 accepted=true
```

## What is NOT changed

- `enable_attach_test_frame_` default remains `true`; the trigger path is unaffected.
- No new mojom fields, no new header dependencies beyond `base/command_line.h`.
- No build-time or runtime behavior changes when the switch is absent.
- Architecture unchanged.

## Suggested next build/run command

**Requires explicit approval before running broad builds.** User approved `-j10` parallelism on 2026-07-04 for compile-only validations (narrow QNX Ozone target). Use `-j10` for any narrow validation builds. Broad `content_shell` build requires separate explicit approval.

**Immediate next: narrow compile validation (user approved `-j10`):**

```bash
cd /home/yuta/chromium/src
# Apply CEF QNX patches and copy source (done in build step)
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_gpu_trace --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_gpu_trace -j10 \
    ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx
```

**Broad content_shell smoke (requires explicit approval):**

```bash
cd /home/yuta/chromium/src/cef
./tools/cef_create_projects_qnx.sh --build-type Release \
  --qnx-sdp-root /home/yuta/qnx800
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C ../out/qnx_release -j10 content/shell:content_shell
```

## Validation

```bash
git diff --check -- docs/qnx/ozone-out-of-process-gpu-plan.md \
  patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc \
  patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc
```

Result: `DIFF_CHECK_RC:0` — no trailing whitespace, no malformed diff lines.

## Review findings

- No blockers found. Changes are additive and guarded by `IsQnxGpuTraceEnabled()`.
- `base/command_line.h` is a safe Chromium base header available in both browser and GPU process contexts.
- No changes to mojom, no changes to GN build files, no changes to root Chromium.
- `enable_attach_test_frame_` stays `true`; the trace is purely observational.
