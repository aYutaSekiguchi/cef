# Phase 5 QNX Mojo Host Scaffold Audit — 2026-07-03

## Review

- Correct: Current CEF `new_files` inventory under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/` is coherent for the Phase 3-5 QNX Ozone tree. Files present: `BUILD.gn`; `client_native_pixmap_factory_qnx.{cc,h}`; `mojom/BUILD.gn`; `mojom/qnx_gpu.mojom`; `ozone_platform_qnx.{cc,h}`; `qnx_gl_ozone_egl.{cc,h}`; `qnx_gpu_host.{cc,h}`; `qnx_platform_event_source.{cc,h}`; `qnx_render_producer.{cc,h}`; `qnx_screen.{cc,h}`; `qnx_screen_context.{cc,h}`; `qnx_surface_factory.{cc,h}`; `qnx_window.{cc,h}`; `qnx_window_manager.{cc,h}`. No unexpected files were found inside that subtree. The parent Chromium working tree also has a copied `../ui/ozone/platform/qnx/` mirror and modified `../build/config/ozone.gni` / `../ui/ozone/BUILD.gn`; `diff -ru patch/qnx/chromium/new_files/ui/ozone/platform/qnx ../ui/ozone/platform/qnx` produced no output, so the mirror matches the CEF new_files source used for compile validation.

- Correct: The mojom schema still matches the fixed intended widget type: `mojom/qnx_gpu.mojom` imports `ui/gfx/mojom/accelerated_widget.mojom` and uses `gfx.mojom.AcceleratedWidget` for `QnxDmaBufFrame.widget`, `QnxGpuHost.ReportProducerLost`, and all `QnxGpuControl` widget parameters (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:13-14`, `:38-46`, `:66-82`, `:87-103`). This matches the prior Mojo fix report requirement (`docs/qnx/history/research/qnx-ozone-phase5-mojo-fix-2026-07-03.md:8-15`, `:33-51`).

- Correct: `qnx_gpu_host.{cc,h}` appear complete for a metadata-only `QnxGpuHost` receiver scaffold. The header subclasses `qnx::QnxGpuHost`, exposes `Bind(mojo::PendingReceiver<qnx::QnxGpuHost>)`, and implements both mojom methods (`SubmitFrame` and `ReportProducerLost`) (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.h:41-61`). The implementation binds a `mojo::Receiver` (`qnx_gpu_host.cc:29-33`), validates frame metadata (`qnx_gpu_host.cc:39-126`), checks widget/generation state (`qnx_gpu_host.cc:148-200`), and explicitly returns `accepted=false` for valid frames because EGL import/display is deferred (`qnx_gpu_host.cc:202-213`). During validation, `../out/qnx_phase5_gpu/obj/ui/ozone/platform/qnx/qnx/qnx_gpu_host.o` existed and was timestamped from this audit run, but the full target failed on other objects.

- Correct: `BUILD.gn` includes the new host scaffold files and depends on the single subdirectory mojom target (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn:48-69`). The earlier duplicate mojom-target issue remains fixed per the current parent BUILD and `mojom/BUILD.gn` (`mojom/BUILD.gn:14-23`).

- Blocker: The current QNX platform target does not compile. `qnx_gl_ozone_egl.cc` still uses `NOTREACHED_IN_MIGRATION()` at `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc:48`; the actual QNX build reports `error: use of undeclared identifier 'NOTREACHED_IN_MIGRATION'`. This contradicts the GPU producer scaffold report, which says this was replaced with `NOTREACHED()` (`docs/qnx/history/research/qnx-ozone-phase5-gpu-producer-scaffold-2026-07-03.md:56-59`).

- Blocker: The current QNX platform target also fails compiling `qnx_render_producer.o`. `qnx_render_producer.h` conditionally includes `<EGL/eglext.h>` without first including `EGL/egl.h` (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.h:20-25`), and the build reports unknown EGL base types from `third_party/angle/include/EGL/eglext.h` such as `EGLDisplay`, `EGLenum`, and `EGLBoolean`. This also contradicts the GPU producer scaffold report, which says EGL type conflicts were fixed by using full EGL includes (`docs/qnx/history/research/qnx-ozone-phase5-gpu-producer-scaffold-2026-07-03.md:56-58`).

- Blocker: `OzonePlatformQnx::AddInterfaces` is implemented, but the binding appears wired to the wrong process semantics for this interface. The QNX code assumes `AddInterfaces` is called in the browser process and only registers `QnxGpuHost` when `has_initialized_ui() && qnx_gpu_host_` (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:176-190`). In current Chromium, `content::ExposeGpuInterfacesToBrowser()` calls `OzonePlatform::GetInstance()->AddInterfaces(binders)` from the GPU-side browser-exposed GPU interface path (`../content/gpu/browser_exposed_gpu_interfaces.cc:18-30`), and the Ozone public comment describes embedders exporting Ozone mojo implementations through that binder map (`../ui/ozone/public/ozone_platform.h:350-360`). In an out-of-process GPU run, the GPU process will not have `InitializeUI()` state or `qnx_gpu_host_`, so the current QNX `AddInterfaces` guard makes registration a no-op and does not provide a browser-side receiver for GPU→Browser `QnxGpuHost::SubmitFrame`. A browser-side host may need a different browser-owned binding path rather than GPU-exposed `AddInterfaces`.

- Note: `QnxGpuHost::ReportProducerLost` has a diagnostic off-by-one after mutation. It calls `SetGpuAttached(widget, false, 0)` and `IncrementGeneration(widget)` (`qnx_gpu_host.cc:247-252`), then logs `(record->generation + 1)` even though `record` points at the map entry that was already incremented (`qnx_gpu_host.cc:254-256`). This should log the new generation directly or capture the old generation before incrementing.

- Note: The host scaffold cannot currently accept a real submitted frame even if the Mojo pipe existed, because `QnxWidgetRecord` starts with `generation = 0` and `gpu_attached = false` (`qnx_window_manager.h:29-41`), while `SubmitFrame` rejects generation `0` (`qnx_gpu_host.cc:46-52`), rejects mismatched generation (`qnx_gpu_host.cc:162-176`), and returns false when `gpu_attached` is false (`qnx_gpu_host.cc:189-200`). No current non-test caller sets `gpu_attached=true` or initializes a generation for attach; only `ReportProducerLost` detaches/increments (`qnx_gpu_host.cc:251-252`). This is acceptable only if the current substep is strictly metadata-only and a follow-up implements `QnxGpuControl` attach/resize/detach.

- Note: `QnxGpuHost::Bind` owns a single `mojo::Receiver` (`qnx_gpu_host.h:73-75`, `qnx_gpu_host.cc:29-33`). If GPU restart/reconnect can create a second receiver while the first is still bound, this will need an explicit reset/disconnect policy or a `mojo::ReceiverSet`.

## Validation commands and summarized output

```sh
cd /home/yuta/chromium/src/cef
git status --short
```

Summary: existing tree was already dirty before this audit, including untracked QNX docs/new_files and `patch/patch.cfg` modified. No source edits were made by this audit before writing this report.

```sh
find patch/qnx/chromium/new_files/ui/ozone/platform/qnx -maxdepth 2 -type f | sort
```

Summary: produced the inventory listed above; no unexpected files inside the QNX Ozone subtree.

```sh
diff -ru patch/qnx/chromium/new_files/ui/ozone/platform/qnx ../ui/ozone/platform/qnx | sed -n '1,240p'
```

Summary: no output; the Chromium working-tree copy matches the CEF `new_files` source.

```sh
ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx
```

Summary: failed during GN regeneration because the environment omitted QNX variables: `QNX_HOST must be set for QNX compiler-rt builtins`.

```sh
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx
```

Summary: failed. Representative errors: `qnx_render_producer.o` failed with unknown EGL base types from `third_party/angle/include/EGL/eglext.h`; `qnx_gl_ozone_egl.o` failed with `use of undeclared identifier 'NOTREACHED_IN_MIGRATION'` at `qnx_gl_ozone_egl.cc:48`.

```sh
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx -k 20 \
  > /tmp/qnx_phase5_host_audit_ninja_k20.log 2>&1
```

Summary: exit code 1. Continued build showed the mojom object and many dependencies built, but the target stopped with the same two QNX object failures. `../out/qnx_phase5_gpu/obj/ui/ozone/platform/qnx/qnx/qnx_gpu_host.o` and `ozone_platform_qnx.o` were present from this audit run, so the newly added host file itself compiled, but the aggregate `ui/ozone/platform/qnx:qnx` validation did not pass.

## Residual risks

- The browser-side host binding cannot be accepted until the compile blockers are fixed and the target is rebuilt successfully.
- Even after compile fixes, the current `AddInterfaces` approach likely does not bind `QnxGpuHost` in the browser process for out-of-process GPU; process-direction/design review is required before accepting the Mojo host scaffold.
- `QnxGpuControl` attach/resize/detach remains absent, so generation and `gpu_attached` state are not usable for real frame acceptance yet.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings cite qnx_gl_ozone_egl.cc:48, qnx_render_producer.h:20-25, ozone_platform_qnx.cc:176-190, qnx_gpu_host.h:41-61, qnx_gpu_host.cc:29-33 and :247-256, plus validation command failures."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-phase5-mojo-host-audit-2026-07-03.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "git status --short",
      "result": "passed",
      "summary": "Inspected existing dirty/untracked tree state before report write."
    },
    {
      "command": "find patch/qnx/chromium/new_files/ui/ozone/platform/qnx -maxdepth 2 -type f | sort",
      "result": "passed",
      "summary": "Collected QNX Ozone new_files inventory."
    },
    {
      "command": "diff -ru patch/qnx/chromium/new_files/ui/ozone/platform/qnx ../ui/ozone/platform/qnx | sed -n '1,240p'",
      "result": "passed",
      "summary": "No output; parent Chromium copy matches CEF new_files."
    },
    {
      "command": "ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx",
      "result": "failed",
      "summary": "Failed at GN regeneration because QNX_HOST was not set."
    },
    {
      "command": "QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 QNX_TARGET=/home/yuta/qnx800/target/qnx ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx",
      "result": "failed",
      "summary": "Target failed on qnx_render_producer.o EGL include/type errors and qnx_gl_ozone_egl.o NOTREACHED_IN_MIGRATION."
    },
    {
      "command": "QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 QNX_TARGET=/home/yuta/qnx800/target/qnx ninja -C ../out/qnx_phase5_gpu ui/ozone/platform/qnx:qnx -k 20 > /tmp/qnx_phase5_host_audit_ninja_k20.log 2>&1",
      "result": "failed",
      "summary": "Exit code 1; confirmed same compile blockers while qnx_gpu_host.o/ozone_platform_qnx.o were generated."
    }
  ],
  "validationOutput": [
    "qnx_gl_ozone_egl.cc:48:3: error: use of undeclared identifier 'NOTREACHED_IN_MIGRATION'",
    "third_party/angle/include/EGL/eglext.h:44:62: error: unknown type name 'EGLDisplay' while compiling qnx_render_producer.o",
    "ui/ozone/platform/qnx/mojom/mojom/qnx_gpu.mojom.o built before the final qnx target failure",
    "qnx_gpu_host.o and ozone_platform_qnx.o were present from this audit run"
  ],
  "residualRisks": [
    "Compile blockers prevent accepting the current partial scaffold.",
    "AddInterfaces appears to run on the GPU-exposed-interface path, so the current has_initialized_ui guard likely makes QnxGpuHost binding a no-op in out-of-process GPU.",
    "QnxGpuControl attach/generation state is not implemented; QnxGpuHost cannot accept real frames yet."
  ],
  "noStagedFiles": true,
  "diffSummary": "Added the requested audit report only; no source files or plan docs were edited by this audit.",
  "reviewFindings": [
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gl_ozone_egl.cc:48 - NOTREACHED_IN_MIGRATION is undeclared and breaks ui/ozone/platform/qnx:qnx.",
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_render_producer.h:20-25 - includes eglext.h without EGL base type definitions, causing unknown EGLDisplay/EGLenum/EGLBoolean errors.",
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:176-190 - AddInterfaces is guarded for browser UI state, but Chromium calls Ozone AddInterfaces from the GPU browser-exposed interface path; likely no QnxGpuHost binding in out-of-process GPU.",
    "medium: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc:247-256 - ReportProducerLost logs generation + 1 after already incrementing the record.",
    "note: qnx_gpu_host.{cc,h} match the QnxGpuHost mojom methods for metadata-only scaffold, but real frame acceptance awaits QnxGpuControl attach/generation wiring."
  ],
  "manualNotes": "Read-only audit respected the instruction not to edit docs/qnx/ozone-out-of-process-gpu-plan.md or source files."
}
```
