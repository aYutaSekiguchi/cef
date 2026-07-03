# QNX Ozone Phase 5 Attach/Generation Compile-Fix — 2026-07-03

## Task

Two bounded compile blockers were identified by the Phase 5 attach/generation audit (`qnx-ozone-phase5-attach-generation-audit-2026-07-03.md`) and required fix-only microtask before attach/generation lifecycle plumbing can be accepted.

## Changes Made

### Fix 1: `qnx_gpu_platform_support_host.cc` — `QnxGpuControl` binder call

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc`

**Problem:** The code at Step 3 attempted to construct a `mojo::ScopedMessagePipeHandle` from `control_receiver.Pipe().get()`, but `mojo::PendingReceiver<QnxGpuControl>` has no `Pipe()` member. The `Pipe()` accessor exists on `mojo::PendingRemote`, not `PendingReceiver`. The compiler error was:

```
error: no member named 'Pipe' in 'mojo::PendingReceiver<ui::ozone::qnx::mojom::QnxGpuControl>'
```

**Fix:** Replaced the broken `Pipe().get()` / `ScopedMessagePipeHandle` construction with a direct `PassPipe()` call, exactly mirroring the already-correct `QnxGpuService` binding at Step 2:

```cpp
// Before (broken):
mojo::PendingReceiver<qnx::QnxGpuControl> control_receiver =
    gpu_control_remote_.BindNewPipeAndPassReceiver();
binder.Run(qnx::QnxGpuControl::Name_,
           mojo::ScopedMessagePipeHandle(control_receiver.Pipe().get()));

// After (fixed):
mojo::PendingReceiver<qnx::QnxGpuControl> control_receiver =
    gpu_control_remote_.BindNewPipeAndPassReceiver();
binder.Run(qnx::QnxGpuControl::Name_, control_receiver.PassPipe());
```

`PassPipe()` on `PendingReceiver<T>` yields the owned `ScopedMessagePipeHandle` and passes it to the binder, which IPC-sends a `dup(2)` of the OS handle to the GPU process. The `PendingReceiver` is then consumed. This is the correct Chromium Mojo pattern for passing a pending receiver's pipe through a `GpuHostBindInterfaceCallback`.

### Fix 2: `qnx_gpu_host.cc` — Generation equality validation in `SubmitFrame`

**File:** `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc`

**Problem:** `SubmitFrame` checked `frame->generation != 0` in `ValidateFrameMetadata` (Step 1), but after widget record lookup (Step 2) it never compared `frame->generation` to `record->generation`. This meant a stale frame from a restarted GPU process could pass to the display-deferred return even when the generation no longer matched. The comments at lines 171–174 and 197–203 claimed "generation already matched" — but no such check existed.

**Fix:** Added a new Step 3 (Generation equality check) between the widget lookup and the GPU-attached check:

```cpp
// ---- Step 3: Generation equality check ----
// Frames must carry the current widget generation to be accepted.
// A stale generation means the frame is from a GPU process that has
// already been replaced.
if (frame->generation != record->generation) {
  DLOG(WARNING) << "QnxGpuHost::SubmitFrame: stale generation for widget="
                << frame->widget << ": frame_gen=" << frame->generation
                << " record_gen=" << record->generation;
  std::move(callback).Run(
      false, "ERROR_STALE_GENERATION: frame generation=" +
                std::to_string(frame->generation) +
                " does not match widget generation=" +
                std::to_string(record->generation));
  return;
}
```

The GPU-attached check was promoted from Step 3 → Step 4, and the size-consistency check from Step 4 → Step 5. The final "Validation passed" block comment was updated to reference the correct step numbers and accurately state that generation was checked.

## Validation

### Whitespace check
```bash
cd /home/yuta/chromium/src && git diff --check -- cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/
```
Result: **passed** — no whitespace errors.

### Temporary root compile validation

1. Root Chromium GN files confirmed clean (`build/config/ozone.gni`, `ui/ozone/BUILD.gn`).
2. Phase 3 GN patches applied cleanly:
   ```bash
   git apply -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
   git apply -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
   ```
3. QNX new files copied to root source tree:
   ```bash
   mkdir -p ui/ozone/platform/qnx
   cp -R cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/
   ```
4. GN generation:
   ```bash
   QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
   QNX_TARGET=/home/yuta/qnx800/target/qnx \
     gn gen out/qnx_phase5_attach_generation_fix \
       --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
   ```
   Result: `Done. Made 33099 targets from 4278 files in 2705ms`

5. Ninja build:
   ```bash
   QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
   QNX_TARGET=/home/yuta/qnx800/target/qnx \
     ninja -C out/qnx_phase5_attach_generation_fix \
       ui/ozone/platform/qnx/mojom:mojom \
       ui/ozone/platform/qnx:qnx
   ```
   Result: **798/798 targets compiled successfully.** Both `qnx_gpu_platform_support_host.o` (68.5 KB) and `qnx_gpu_host.o` (93.7 KB) are present in the output directory with no errors.

### Post-validation cleanup
- `ui/ozone/platform/qnx/` removed from root source tree.
- `out/qnx_phase5_attach_generation_fix/` left in place (can be reused for further Phase 5 validation; no source tree pollution).
- GN patches reverted; root Chromium GN files confirmed clean.

## Compile Target Status

| Target | Before fix | After fix |
|--------|-----------|-----------|
| `ui/ozone/platform/qnx/mojom:mojom` | ✅ pass | ✅ pass |
| `ui/ozone/platform/qnx:qnx` (qnx_gpu_platform_support_host.o) | ❌ `no member named 'Pipe'` | ✅ pass |
| `ui/ozone/platform/qnx:qnx` (qnx_gpu_host.o) | ✅ pass | ✅ pass |

## Metadata-Only SubmitFrame Validation Path

With both fixes applied, a metadata-only `SubmitFrame` call can now pass the following ordered validation chain:

1. **Null frame** → `ERROR_NULL_FRAME`
2. **Metadata** (null widget, zero generation, zero dimension, dimension overflow, no planes, per-plane fd/stride/offset) → diagnostic string
3. **Widget record exists** → `ERROR_UNKNOWN_WIDGET`
4. **Generation equality** (frame vs. record) → `ERROR_STALE_GENERATION`
5. **GPU attached** → `ERROR_GPU_NOT_ATTACHED`
6. **Size consistency** (advisory only; logs and continues)
7. **All checks passed** → `accepted=false, "QNX host: VALID metadata; accepted=false; import/display deferred"`

This means the metadata-only path intentionally returns `display-deferred` (not a rejection) when widget/generation/attached are all valid — which is the correct Phase 5 scaffold behavior. Real EGL import and Screen display remain deferred to a subsequent Phase 5 substep.

## Remaining Work

- **Real GPU `SubmitFrame` caller**: `QnxGpuService` currently logs `AttachWidget`/`ResizeWidget`/`DetachWidget` only; GPU-side render resource creation and real `SubmitFrame` production calls are still stubbed.
- **Browser EGL import/display**: `QnxGpuHost::SubmitFrame` returns `accepted=false` with deferred diagnostic; actual DMAbuf import via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` and Screen window display via `eglSwapBuffers()` are not implemented.
- **Runtime out-of-process GPU smoke**: Requires a real `cefsimple --ozone-platform=qnx` run under QEMU virgl with `--ozone-platform=qnx` and no `--in-process-gpu`.
- **Crash/restart**: `QnxWindowManager::DetachAllWidgets()` and `QnxGpuHost::ReportProducerLost()` are wired; runtime crash smoke is still pending.
- **GPU-side `AttachWidget`/`ResizeWidget` handler wiring**: `QnxGpuService::AttachWidget` is logging-only; it needs to connect to `QnxRenderProducer` creation.

## Files Changed

| File | Change |
|------|--------|
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc` | Replaced broken `Pipe().get()` / `ScopedMessagePipeHandle` with `control_receiver.PassPipe()` for the `QnxGpuControl` binder call. Updated comment to reflect correct pattern. |
| `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc` | Added Step 3 generation equality check between widget lookup and GPU-attached check. Renumbered Step 3 → Step 4, Step 4 → Step 5. Updated comments to reflect actual validation order. |
