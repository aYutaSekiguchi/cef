# QNX Ozone Phase 5 Mojo Service Binding — 2026-07-03

## Files Changed (CEF-managed, under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`)

| File | Change |
|------|--------|
| `mojom/qnx_gpu.mojom` | Added `QnxGpuService` interface with `Initialize(pending_remote<QnxGpuHost> host_remote)`. Existing `QnxGpuHost` and `QnxGpuControl` kept unchanged. |
| `qnx_gpu_service.h` | New GPU-side header. Declares `QnxGpuService` class (implements `qnx::QnxGpuService`), with `Bind()`, `Initialize()`, and `gpu_host_remote()` accessor. |
| `qnx_gpu_service.cc` | New GPU-side implementation. Stores `mojo::Remote<QnxGpuHost>` from `Initialize()`. Sets disconnect handler. No real SubmitFrame runtime. |
| `qnx_gpu_platform_support_host.h` | New browser-side header. Declares `QnxGpuPlatformSupportHost` implementing `ui::GpuPlatformSupportHost`. Owns `QnxGpuHost` and bridges to GPU-side `QnxGpuService`. |
| `qnx_gpu_platform_support_host.cc` | New browser-side implementation. `OnGpuServiceLaunched()` creates `QnxGpuHost`, gets its `PendingRemote`, binds GPU-side `QnxGpuService` via binder, calls `Initialize(host_remote)`. `OnChannelDestroyed()` resets service remote and marks widgets detached. |
| `qnx_gpu_host.h` | Added `GetPendingRemote()` returning `mojo::PendingRemote<qnx::QnxGpuHost>` via `receiver_.BindNewPipeAndPassRemote()`. Added `mojo/public/cpp/bindings/pending_remote.h` include. |
| `qnx_gpu_host.cc` | Added `QnxGpuHost::GetPendingRemote()` implementation. |
| `ozone_platform_qnx.cc` | Replaced `CreateStubGpuPlatformSupportHost()` with `QnxGpuPlatformSupportHost`. Removed old `qnx_gpu_host_` member (moved into connector). Changed `AddInterfaces()` to register GPU-side `QnxGpuService` via lazy-create binder pattern (matching DRM platform). Updated includes. |
| `BUILD.gn` | Added `qnx_gpu_service.{cc,h}` and `qnx_gpu_platform_support_host.{cc,h}` to source_set("qnx") sources list. |

## Binding Architecture Summary

```
Browser process (InitializeUI)
  OzonePlatformQnxImpl
    QnxGpuPlatformSupportHost  ← owns QnxGpuHost (browser-owned)
      qnx_gpu_host_ (unique_ptr<QnxGpuHost>)
        mojo::Receiver<QnxGpuHost>  ← browser end
      gpu_service_remote_ (mojo::Remote<QnxGpuService>)
      OnGpuServiceLaunched():
        1. qnx_gpu_host_ = make_unique<QnxGpuHost>()
        2. host_remote = qnx_gpu_host_->GetPendingRemote()
        3. binder(QnxGpuService.Name_, receiver)
        4. gpu_service_remote_->Initialize(host_remote)
      OnChannelDestroyed():
        1. gpu_service_remote_.reset()
        2. MarkAllWidgetsGpuDetached()
        3. qnx_gpu_host_.reset()

GPU process (InitializeGPU + AddInterfaces)
  OzonePlatformQnxImpl
    gpu_surface_factory_ (QnxSurfaceFactoryOzone)
    AddInterfaces():
      binders->Add<QnxGpuService>(lazy_bind_callback, gpu_task_runner)
        → creates QnxGpuService, binds receiver
          QnxGpuService
            mojo::Receiver<QnxGpuService>
            gpu_host_remote_ (mojo::Remote<QnxGpuHost>)
            Initialize(host_remote):
              gpu_host_remote_.Bind(host_remote)
              gpu_host_remote_.set_disconnect_handler(...)
```

**Key design decisions:**

- Browser owns `QnxGpuHost` (receiver lives in browser). GPU holds the remote and calls `SubmitFrame`/`ReportProducerLost`.
- GPU exposes `QnxGpuService` via `AddInterfaces` (DRM/Ozone pattern). Browser obtains the GPU remote through `OnGpuServiceLaunched`'s binder.
- `QnxGpuService::Initialize(pending_remote<QnxGpuHost>)` hands the browser host remote to the GPU — exactly the Wayland `WaylandBufferManagerGpu::Initialize()` pattern.
- `QnxGpuService` is lazily created in the `AddInterfaces` binder callback (DRM pattern), alive for the receiver pipe lifetime.
- Single `mojo::Receiver<QnxGpuHost>` in browser, reset/recreated on GPU restart via `QnxGpuPlatformSupportHost::OnGpuServiceLaunched()`.
- No EGL import, no Screen display, no real `SubmitFrame` calls — compile-safe plumbing only.

## Validation

### Commands

**1. Git diff check (CEF managed files):**
```sh
git diff --check -- patch/qnx/chromium/new_files/ui/ozone/platform/qnx/
```
Result: No whitespace errors.

**2. Apply Phase 3 GN patches to Chromium root:**
- Edited `build/config/ozone.gni`: added `ozone_platform_qnx = false` and updated assert.
- Edited `ui/ozone/BUILD.gn`: added `if (ozone_platform_qnx) { ... }` block.

**3. Copy CEF new_files into Chromium root:**
```sh
cp -r patch/qnx/chromium/new_files/ui/ozone/platform/qnx/* \
      /home/yuta/chromium/src/ui/ozone/platform/qnx/
```

**4. GN generation:**
```sh
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_mojo_service \
    --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
```
Result: `Done. Made 33099 targets from 4278 files in 2688ms` — succeeded.

**5. Ninja build:**
```sh
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_mojo_service \
    ui/ozone/platform/qnx/mojom:mojom \
    ui/ozone/platform/qnx:qnx
```
Result: `800/800` targets built — exit 0, succeeded.

**Object files confirmed:**
- `obj/ui/ozone/platform/qnx/qnx/qnx_gpu_service.o` (34.1K)
- `obj/ui/ozone/platform/qnx/qnx/qnx_gpu_platform_support_host.o` (52.8K)
- `obj/ui/ozone/platform/qnx/qnx/qnx_gpu_host.o` (93.6K)
- `obj/ui/ozone/platform/qnx/qnx/ozone_platform_qnx.o` (140.0K)
- `obj/ui/ozone/platform/qnx/mojom/mojom/qnx_gpu.mojom.o` (205.4K)

**6. Cleanup:**
- Removed all temporary root-source files (`ui/ozone/platform/qnx/`)
- Removed build output directory (`out/qnx_phase5_mojo_service/`)
- Reverted Phase 3 GN patches (`build/config/ozone.gni`, `ui/ozone/BUILD.gn`)
- Chromium root verified clean

### Compilation Errors Fixed During Validation

**Error 1 — Namespace resolution in `qnx_gpu_platform_support_host.h`:**
```
error: use of undeclared identifier 'qnx'; did you mean 'ozone::qnx'?
```
The shortcut `namespace qnx = ui::ozone::qnx::mojom` inside `namespace ui {}` creates `ui::qnx` (not usable as `qnx::` in isolation). Fixed by using fully-qualified `ui::ozone::qnx::mojom::QnxGpuHost` / `ui::ozone::qnx::mojom::QnxGpuService` in the header.

**Error 2 — `base::ThreadTaskRunnerHandle::Get()` not found:**
```
error: no member named 'ThreadTaskRunnerHandle' in namespace 'base'
```
`ThreadTaskRunnerHandle` is not present in this Chromium compatibility tag. Fixed by using `base::SingleThreadTaskRunner::GetCurrentDefault()` (matching the DRM platform pattern at `ozone_platform_drm.cc:229`).

## What Remains

### Real frame SubmitFrame calls (GPU side)
`QnxGpuService` stores `mojo::Remote<QnxGpuHost>` but no code yet calls `gpu_host_remote_->SubmitFrame(...)`. GPU-side render loop must be wired to populate `QnxDmaBufFrame` and call `gpu_host_remote_->SubmitFrame()`.

### QnxGpuControl attach/generation
Widget records start with `generation=0` and `gpu_attached=false`. `SubmitFrame` always rejects generation-zero frames. `QnxGpuControl` (already defined in mojom) needs a browser-side client implementation that calls `window_manager_->SetGpuAttached()` and `window_manager_->IncrementGeneration()` on `AttachWidget`/`DetachWidget`.

### Browser EGL import/display
`QnxGpuHost::SubmitFrame` currently returns `accepted=false`. Real implementation must call `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` for each plane fd, create a GL texture, and render to the Screen/EGL window surface.

### Crash/restart runtime smoke
`OnChannelDestroyed` resets the service remote and marks widgets detached, but this has not been exercised at runtime. A smoke run with `--ozone-platform=qnx` out-of-process GPU is needed to confirm the reconnect path.

### In-process-GPU acceptance
Not accepted as validation for this binding path. The binding path is specifically designed for out-of-process GPU.

## Blockers / Requested Plan Updates

None at compile level. The compile succeeds cleanly.

Runtime blockers:
- **No GPU-side SubmitFrame caller**: the `gpu_host_remote_` is stored but never called. A GPU-side render loop stub is needed to exercise the binding.
- **No QnxGpuControl client**: `QnxGpuControl` mojom interface is defined but no browser-side client exists. `QnxWidgetRecord.gpu_attached` remains false and generation stays at 0.
- **No EGL import/display**: `QnxGpuHost::SubmitFrame` always returns false. Real display requires Screen/EGL integration on the browser side.

## Recommended Next Step

Add Phase 5 attach/generation substep:
1. Implement `QnxGpuControlClient` in the browser process (client of `QnxGpuControl` from GPU).
2. In `QnxGpuPlatformSupportHost::OnGpuServiceLaunched()`, bind GPU-side `QnxGpuControl` receiver and store the client remote.
3. On `AttachWidget`, call `window_manager_->SetGpuAttached(widget, true, pid)` and `window_manager_->IncrementGeneration(widget)`.
4. On `DetachWidget`, call `window_manager_->SetGpuAttached(widget, false, 0)`.
5. Increment generation on `OnChannelDestroyed`.

Alternatively, proceed to Phase 6 visual smoke and implement attach/generation as part of the Phase 6 crash recovery step.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings: 9 CEF-managed source files edited/added in patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. gn gen + ninja build succeeded for both mojom:mojom (800/800 targets). Object files confirmed: qnx_gpu_service.o (34.1K), qnx_gpu_platform_support_host.o (52.8K), qnx_gpu_host.o (93.6K), ozone_platform_qnx.o (140.0K), qnx_gpu.mojom.o (205.4K). Two compile errors fixed during validation: namespace shortcut resolution and ThreadTaskRunnerHandle API change."
    }
  ],
  "changedFiles": [
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.h",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.h",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.h",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc",
    "patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git diff --check -- patch/qnx/chromium/new_files/ui/ozone/platform/qnx/",
      "result": "passed",
      "summary": "No whitespace errors in changed CEF-managed files."
    },
    {
      "command": "gn gen out/qnx_phase5_mojo_service --args='...ozone_platform_qnx=true'",
      "result": "passed",
      "summary": "GN generation succeeded: 33099 targets from 4278 files."
    },
    {
      "command": "ninja -C out/qnx_phase5_mojo_service ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx",
      "result": "passed",
      "summary": "800/800 targets built, exit 0. Object files: qnx_gpu_service.o (34.1K), qnx_gpu_platform_support_host.o (52.8K), qnx_gpu_host.o (93.6K), ozone_platform_qnx.o (140.0K), qnx_gpu.mojom.o (205.4K)."
    },
    {
      "command": "cleanup: remove temp root-source files, revert GN patches, remove build dir",
      "result": "passed",
      "summary": "Chromium root verified clean after cleanup. QNX platform dir removed. GN patches reverted."
    }
  ],
  "validationOutput": [
    "gn gen: Done. Made 33099 targets from 4278 files in 2688ms",
    "ninja: 800/800 targets built, exit 0",
    "Fixed compile error 1: namespace shortcut qnx:: inside namespace ui {} creates ui::qnx alias; header now uses fully-qualified ui::ozone::qnx::mojom::*",
    "Fixed compile error 2: base::ThreadTaskRunnerHandle not present in this Chromium tag; replaced with base::SingleThreadTaskRunner::GetCurrentDefault()",
    "All temporary root-source files cleaned up after validation"
  ],
  "residualRisks": [
    "QnxGpuService stores gpu_host_remote_ but no GPU-side code calls SubmitFrame yet",
    "QnxGpuControl interface is defined in mojom but has no browser-side client; gpu_attached remains false and generation stays at 0",
    "QnxGpuHost::SubmitFrame always returns accepted=false; real EGL import/display not implemented",
    "Runtime binding path not exercised (no out-of-process GPU smoke run yet)"
  ],
  "noStagedFiles": true,
  "diffSummary": "Phase 5 Mojo service binding: added QnxGpuService GPU-side startup interface, QnxGpuPlatformSupportHost browser-side GpuPlatformSupportHost bridge, GetPendingRemote() helper on QnxGpuHost, updated AddInterfaces() to use DRM-style lazy-create GPU service binder, updated BUILD.gn. Architecture follows Wayland/DRM pattern: browser owns QnxGpuHost receiver, GPU exposes QnxGpuService via AddInterfaces, browser passes host remote to GPU through Initialize() call.",
  "reviewFindings": [
    "blocker: none — all compile errors fixed during validation",
    "note: namespace shortcut qnx:: inside namespace ui {} creates ui::qnx alias, not ui::ozone::qnx::mojom:: shortcut; header uses fully-qualified names to avoid resolution ambiguity",
    "note: base::ThreadTaskRunnerHandle is absent in this Chromium tag; used base::SingleThreadTaskRunner::GetCurrentDefault() (matching DRM platform pattern)"
  ],
  "manualNotes": "Validation succeeded. Browser-owned QnxGpuHost / GPU-exposed QnxGpuService binding path is compile-safe. Runtime binding (SubmitFrame plumbing, QnxGpuControl attach/generation, EGL import/display) remains deferred to subsequent Phase 5/6 substeps. Cleanup confirmed: Chromium root is clean, no staged files."
}
```
