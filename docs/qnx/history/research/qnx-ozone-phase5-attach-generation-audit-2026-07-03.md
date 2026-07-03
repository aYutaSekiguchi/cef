# QNX Ozone Phase 5 Attach/Generation Partial Audit — 2026-07-03

## Review

- Correct: The active plan explicitly marks attach/generation lifecycle plumbing as unaccepted after a timeout and requires this read-only audit/compile check before acceptance (`docs/qnx/ozone-out-of-process-gpu-plan.md:368`, `docs/qnx/ozone-out-of-process-gpu-plan.md:395`, `docs/qnx/ozone-out-of-process-gpu-plan.md:478`).
- Correct: The previous accepted binding report states the baseline `QnxGpuService.Initialize(pending_remote<QnxGpuHost>)` startup path compiled, but left `QnxGpuControl`, real `SubmitFrame`, and EGL import/display deferred (`docs/qnx/history/research/qnx-ozone-phase5-mojo-service-binding-2026-07-03.md`).
- Blocker: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc:111-118` does not compile. It attempts `control_receiver.Pipe().get()`, but `mojo::PendingReceiver<QnxGpuControl>` has no `Pipe()` member in this Chromium tag. Bounded compile validation fails at `qnx_gpu_platform_support_host.o` with: `error: no member named 'Pipe' in 'mojo::PendingReceiver<ui::ozone::qnx::mojom::QnxGpuControl>'`.
- Blocker: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc:117-118` does not use `GpuHostBindInterfaceCallback` correctly for `QnxGpuControl`. The `QnxGpuService` path is correct (`BindNewPipeAndPassReceiver()` once at `qnx_gpu_platform_support_host.cc:82-84`, then `.PassPipe()` to `binder.Run()`), but the `QnxGpuControl` path must also pass the pending receiver's `.PassPipe()` instead of constructing a new `ScopedMessagePipeHandle` from a raw handle.
- Blocker: `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc:55-61` only rejects generation zero; after widget lookup it never compares `frame->generation` to `record->generation` before the display-deferred return at `qnx_gpu_host.cc:197-210`. The comments at `qnx_gpu_host.cc:171-174` and `qnx_gpu_host.cc:197-203` claim generation already matched, but no such check exists. `SubmitFrame` therefore cannot yet be accepted as passing widget/generation/attached validation.
- Correct: `QnxGpuControl` exists in mojom and has attach/resize/detach methods (`mojom/qnx_gpu.mojom:108-123`). A browser-side remote exists (`qnx_gpu_platform_support_host.h:106-109`), and a GPU-side receiver implementation exists by having `QnxGpuService` implement `qnx::QnxGpuControl` (`qnx_gpu_service.h:36-37`, `qnx_gpu_service.h:65-82`, `qnx_gpu_service.h:96`, `qnx_gpu_service.cc:37-43`, `qnx_gpu_service.cc:84-112`). Method signatures match the mojom-generated C++ shape used elsewhere in the file, but the current control pipe binding does not compile.
- Correct: `QnxWindowManager` now has real all-widget detach/generation helpers, not TODO logging. It declares `DetachAllWidgets()` and a snapshot iterator helper (`qnx_window_manager.h:89-98`), and `DetachAllWidgets()` marks attached widgets detached, clears `gpu_pid`, and increments generation (`qnx_window_manager.cc:104-118`). `GetWidgetRecordsForTestingOrGpuAttach()` returns copied records for attach iteration (`qnx_window_manager.cc:120-128`).
- Note: The current `SubmitFrame` can reach the intentional display-deferred return only when metadata is valid, a widget record exists, and `record->gpu_attached` is true (`qnx_gpu_host.cc:141-184`, `qnx_gpu_host.cc:197-210`). Because generation equality is missing, this path is not sufficient evidence for generation validation.

## Current partial file inventory

`git status --short -- patch/qnx/chromium/new_files/ui/ozone/platform/qnx docs/qnx/history/research/qnx-ozone-phase5-attach-generation-audit-2026-07-03.md` showed the QNX platform new-files tree as untracked before writing this report:

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gl_ozone_egl.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_service.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_platform_event_source.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen_context.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen_context.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_factory.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_surface_factory.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.h`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.h`

No dedicated `qnx_gpu_control.cc` or `qnx_gpu_control.h` file exists. `QnxGpuControl` is implemented inside `QnxGpuService`.

## Validation commands and summarized output

1. `git status --short`
   - Result: repository has many existing untracked QNX docs/new-files artifacts and `patch/patch.cfg` modified; the audited QNX platform tree is untracked.

2. `find patch/qnx/chromium/new_files/ui/ozone/platform/qnx -maxdepth 2 -type f | sort`
   - Result: inventory listed above; no `qnx_gpu_control.*` files.

3. `git diff --check -- patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`
   - Result: passed; no whitespace errors reported.

4. Initial temporary-root compile attempt without `git apply -p0`:
   - Command attempted to apply CEF GN patches, copy `patch/qnx/chromium/new_files/ui/ozone/platform/qnx` into `/home/yuta/chromium/src/ui/ozone/platform/qnx`, run `gn gen`, then run ninja.
   - Result: GN patches did not apply because `git apply` stripped one path component (`error: ozone/BUILD.gn: No such file or directory`, `error: config/ozone.gni: No such file or directory`), `gn gen` still ran, and ninja failed with `unknown target 'ui/ozone/platform/qnx/mojom:mojom'`. Temporary root files/output were cleaned.

5. Corrected bounded compile validation:
   ```sh
   cd /home/yuta/chromium/src
   git diff --quiet -- build/config/ozone.gni ui/ozone/BUILD.gn
   test ! -e ui/ozone/platform/qnx
   git apply --check -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
   git apply --check -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
   git apply -p0 cef/patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch
   git apply -p0 cef/patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch
   mkdir -p ui/ozone/platform/qnx
   cp -R cef/patch/qnx/chromium/new_files/ui/ozone/platform/qnx/. ui/ozone/platform/qnx/
   QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
   QNX_TARGET=/home/yuta/qnx800/target/qnx \
     gn gen out/qnx_phase5_attach_audit \
       --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"
   QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
   QNX_TARGET=/home/yuta/qnx800/target/qnx \
     ninja -C out/qnx_phase5_attach_audit \
       ui/ozone/platform/qnx/mojom:mojom \
       ui/ozone/platform/qnx:qnx
   ```
   - GN result: `Done. Made 33099 targets from 4278 files in 2715ms` with only the existing `cef_target_arch` no-effect warning.
   - Ninja result: failed at `[10069/10118] CXX obj/ui/ozone/platform/qnx/qnx/qnx_gpu_platform_support_host.o`.
   - Error: `../../ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc:118:61: error: no member named 'Pipe' in 'mojo::PendingReceiver<ui::ozone::qnx::mojom::QnxGpuControl>'`.
   - Other observed warnings before the failure were pre-existing style warnings in `qnx_platform_event_source.cc` and `qnx_render_producer.cc`.
   - Cleanup: `/home/yuta/chromium/src/ui/ozone/platform/qnx` and `/home/yuta/chromium/src/out/qnx_phase5_attach_audit` were removed, and the two GN patches were reversed. Post-cleanup checks showed no root QNX temp directory, no audit output dir, and clean root GN files.

## Smallest safe next fix

1. Fix only the `QnxGpuControl` binder call in `qnx_gpu_platform_support_host.cc`:
   ```cpp
   mojo::PendingReceiver<qnx::QnxGpuControl> control_receiver =
       gpu_control_remote_.BindNewPipeAndPassReceiver();
   binder.Run(qnx::QnxGpuControl::Name_, control_receiver.PassPipe());
   ```
   This mirrors the already-correct `QnxGpuService` binding at `qnx_gpu_platform_support_host.cc:82-84` and the existing Chromium Wayland/DRM patterns (`BindNewPipeAndPassReceiver().PassPipe()` / `InitWithNewPipeAndPassReceiver().PassPipe()`).

2. Add the missing generation equality check in `QnxGpuHost::SubmitFrame` after widget lookup and before `gpu_attached`/display-deferred handling:
   ```cpp
   if (frame->generation != record->generation) {
     std::move(callback).Run(false, "ERROR_STALE_GENERATION: ...");
     return;
   }
   ```

3. Re-run the corrected bounded compile command above. Only after that should attach/generation be considered for acceptance; runtime smoke remains separate because `QnxGpuService::AttachWidget`/`ResizeWidget`/`DetachWidget` are still logging-only GPU-side handlers (`qnx_gpu_service.cc:84-112`).

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings cite qnx_gpu_platform_support_host.cc:111-118 compile failure, qnx_gpu_host.cc:55-61 and 157-210 missing generation equality, QnxGpuControl mojom/service/remote line ranges, and QnxWindowManager detach/generation helpers."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-phase5-attach-generation-audit-2026-07-03.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git status --short",
      "result": "passed",
      "summary": "Showed existing untracked QNX docs/new-files artifacts and modified patch/patch.cfg; audited QNX platform tree is untracked."
    },
    {
      "command": "find patch/qnx/chromium/new_files/ui/ozone/platform/qnx -maxdepth 2 -type f | sort",
      "result": "passed",
      "summary": "Inventoried 29 files; no qnx_gpu_control.* file exists."
    },
    {
      "command": "git diff --check -- patch/qnx/chromium/new_files/ui/ozone/platform/qnx/",
      "result": "passed",
      "summary": "No whitespace errors reported."
    },
    {
      "command": "temporary root compile attempt using git apply without -p0, copy qnx new_files, gn gen, ninja",
      "result": "failed",
      "summary": "GN patches did not apply due path stripping; ninja target was unknown. Temporary files were cleaned."
    },
    {
      "command": "temporary root compile validation using git apply -p0, copy qnx new_files, gn gen out/qnx_phase5_attach_audit, ninja -C out/qnx_phase5_attach_audit ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx",
      "result": "failed",
      "summary": "GN succeeded; ninja failed compiling qnx_gpu_platform_support_host.o because PendingReceiver<QnxGpuControl> has no Pipe() member. Temporary files and GN patches were cleaned."
    }
  ],
  "validationOutput": [
    "gn gen: Done. Made 33099 targets from 4278 files in 2715ms",
    "ninja: failed at [10069/10118] CXX obj/ui/ozone/platform/qnx/qnx/qnx_gpu_platform_support_host.o",
    "error: ../../ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc:118:61: no member named 'Pipe' in 'mojo::PendingReceiver<ui::ozone::qnx::mojom::QnxGpuControl>'",
    "cleanup verified: no /home/yuta/chromium/src/ui/ozone/platform/qnx, no out/qnx_phase5_attach_audit, root GN files clean"
  ],
  "residualRisks": [
    "Compile is blocked until the QnxGpuControl binder uses control_receiver.PassPipe().",
    "SubmitFrame does not compare frame generation to widget record generation, so stale frames can reach the display-deferred path when gpu_attached is true.",
    "QnxGpuService QnxGpuControl handlers are logging-only; GPU-side render resource lifecycle is still deferred.",
    "No runtime out-of-process GPU smoke was run."
  ],
  "noStagedFiles": true,
  "diffSummary": "Audit report only. No source files or plan docs were edited.",
  "reviewFindings": [
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc:111-118 - QnxGpuControl binder path does not compile; PendingReceiver has no Pipe() member and should pass control_receiver.PassPipe().",
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc:55-61,157-210 - SubmitFrame lacks frame generation versus record generation validation before display-deferred return.",
    "note: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.cc:104-128 - real all-widget detach/snapshot helpers exist.",
    "note: no qnx_gpu_control.* source files exist; QnxGpuControl is implemented inside QnxGpuService."
  ],
  "manualNotes": "Report written to the requested authoritative path. Temporary Chromium-root validation copies were cleaned after compile attempts."
}
```
