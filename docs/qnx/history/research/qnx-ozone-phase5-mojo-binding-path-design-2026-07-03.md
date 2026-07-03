# QNX Ozone Phase 5 Mojo Binding Path Design — 2026-07-03

## Review

- Correct: The target architecture requires the Browser/UI process to own visible QNX Screen windows and the GPU process to own restartable producer resources. The durable plan states this directly (`docs/qnx/ozone-out-of-process-gpu-plan.md:15-17`, `:33-46`) and also requires `gfx::AcceleratedWidget` to remain a stable numeric ID rather than a `screen_window_t` pointer (`docs/qnx/ozone-out-of-process-gpu-plan.md:49-54`).
- Correct: The Phase 2 design selects typed Mojo IPC, not a backend-owned Unix socket, with `QnxGpuHost.SubmitFrame(QnxDmaBufFrame)` carrying `handle<platform>` DMAbuf fds from GPU to Browser (`docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:210-217`, `:355-368`, `:430-440`).
- Correct: The current mojom direction is conceptually right: `QnxGpuHost` is Browser-side and used by the GPU for `SubmitFrame`, while `QnxGpuControl` is Browser-to-GPU lifecycle control (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom:64-87`).
- Blocker: The current `OzonePlatformQnx::AddInterfaces` binding is wrong for `QnxGpuHost` in out-of-process GPU. It registers a Browser-owned `QnxGpuHost` receiver only when `has_initialized_ui() && qnx_gpu_host_` (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:176-193`), but Chromium calls Ozone `AddInterfaces()` while constructing the GPU child's browser-exposed interface binder map (`content/gpu/gpu_child_thread.cc:213-223`; `content/gpu/browser_exposed_gpu_interfaces.cc:18-30`). In OOP GPU, this binder map lives on the GPU side; the browser-side QNX `InitializeUI()` state and `qnx_gpu_host_` are not present there. The result is either no registration or the wrong process owning the receiver.
- Note: `AddInterfaces()` is still useful for QNX, but only for GPU-implemented interfaces that the browser needs to request. Existing Ozone platforms use it this way: Wayland explicitly notes the call happens on the GPU and registers `WaylandBufferManagerGpu` (`ui/ozone/platform/wayland/ozone_platform_wayland.cc:454-468`), while DRM registers the GPU-side `DrmDevice` receiver from `AddInterfaces()` (`ui/ozone/platform/drm/ozone_platform_drm.cc:97-125`).

## Binding-path conclusion

### Confirm/refute: Is Ozone `AddInterfaces` wrong for `QnxGpuHost`?

Confirmed: `OzonePlatform::AddInterfaces()` is the wrong place to bind the Browser-owned `QnxGpuHost` receiver directly. The evidence is the call chain:

1. `GpuChildThread` builds a `mojo::BinderMap` for "browser-exposed interfaces" after GPU service initialization (`content/gpu/gpu_child_thread.cc:213-223`).
2. That path calls `content::ExposeGpuInterfacesToBrowser()` (`content/gpu/browser_exposed_gpu_interfaces.cc:18-30`).
3. `ExposeGpuInterfacesToBrowser()` calls `ui::OzonePlatform::GetInstance()->AddInterfaces(binders)` under `IS_OZONE` (`content/gpu/browser_exposed_gpu_interfaces.cc:28-30`).
4. Child-side `IOThreadState` uses that binder map to satisfy receiver requests arriving from the browser (`content/child/child_thread_impl.cc:326-334`, `:392-400`).

Therefore, in the multi-process target architecture, `AddInterfaces()` exports GPU-process implementations to the browser. A `QnxGpuHost` receiver must instead be owned and bound in the browser process; the GPU should receive a `pending_remote<QnxGpuHost>` over a browser-to-GPU startup/control interface.

## Plausible integration paths

### Path A — Recommended minimal Ozone-local bridge via `GpuPlatformSupportHost`

Pattern: Wayland/Flatland-style Ozone bridge.

Flow:

1. Browser `OzonePlatformQnx::InitializeUI()` creates `QnxGpuHost` and a new QNX `GpuPlatformSupportHost` implementation instead of a stub.
2. `OzonePlatformQnx::GetGpuPlatformSupportHost()` returns that QNX connector.
3. `components/viz/host/GpuHostImpl::InitOzone()` calls `GetGpuPlatformSupportHost()->OnGpuServiceLaunched(restart_id, binder, terminate_callback)` from the browser UI thread (`components/viz/host/gpu_host_impl.cc:435-451`; API contract at `ui/ozone/public/gpu_platform_support_host.h:40-49`).
4. The QNX connector binds a browser-owned `QnxGpuHost` receiver and obtains `mojo::PendingRemote<qnx::QnxGpuHost>`.
5. The QNX connector uses the supplied `binder` to request a GPU-side `QnxGpuControl`/`QnxGpuService` receiver from the GPU process. This is the same mechanism Wayland uses at `ui/ozone/platform/wayland/host/wayland_buffer_manager_connector.cc:37-65`: it creates the browser-side host remote, binds a GPU-side remote through the `binder`, then calls GPU `Initialize(...)` with the browser host remote. The Wayland mojom documents the direction explicitly: `WaylandBufferManagerHost` is implemented by the browser and used by the GPU (`ui/ozone/platform/wayland/mojom/wayland_buffer_manager.mojom:20-24`), and `WaylandBufferManagerGpu::Initialize(pending_remote<WaylandBufferManagerHost> remote_host, ...)` passes that browser host remote to the GPU (`ui/ozone/platform/wayland/mojom/wayland_buffer_manager.mojom:94-116`).
6. QNX GPU code stores the `mojo::Remote<QnxGpuHost>` and later calls `SubmitFrame()` with DMAbuf platform handles.
7. `OnChannelDestroyed(host_id)` resets the pipe and marks widgets detached/increments generation; Chromium already calls this Ozone hook when the GPU process host is destroyed (`content/browser/gpu/gpu_process_host.cc:368-375`).

Pros:

- Ozone-local and consistent with existing Ozone process-direction patterns.
- Preserves the Browser-owned visible Screen window and Browser-owned `QnxGpuHost` receiver.
- Preserves out-of-process GPU because the connection is established through Chromium's normal GPU-process launch path, not `--in-process-gpu`.
- Uses Mojo `handle<platform>` for DMAbuf fds; no custom Unix socket.
- Gives QNX a natural restart hook via `host_id` / `restart_id` and `OnChannelDestroyed()`.
- Avoids content/browser layering changes.

Cons / risks:

- Requires adding a small GPU-side control/service implementation and one browser-side connector class.
- The current `QnxGpuControl` mojom lacks an `Initialize(pending_remote<QnxGpuHost>)` method, so the next implementation must either extend `QnxGpuControl` or introduce a `QnxGpuService` interface.
- `QnxGpuHost` currently owns a single `mojo::Receiver`; the connector should reset/rebind on GPU restart, or convert it to a `mojo::ReceiverSet` if overlapping reconnects are possible.
- Widget generation/attach state must be initialized during the same handshake. Current widget records start as `generation = 0` and `gpu_attached = false` (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.h:29-41`), while `SubmitFrame()` rejects generation zero (`patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc:46-52`).

Likely file touch points:

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.{h,cc}`
- new `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.{h,cc}` (name flexible)
- new `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_control.{h,cc}` or `qnx_gpu_service.{h,cc}` for the GPU-side receiver
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.{h,cc}` for attach/generation helpers if existing setters are insufficient
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`

### Path B — GPU requests browser host receiver through `ContentBrowserClient::BindGpuHostReceiver`

Pattern: child-to-browser host receiver request.

Flow:

1. GPU-side QNX code creates a `mojo::Remote<QnxGpuHost>` and sends the receiver to the browser via `content::ChildThread::Get()->BindHostReceiver(...)`.
2. Browser-side `GpuProcessHost::BindHostReceiver()` receives the generic request and forwards unhandled GPU host requests to `GetContentClient()->browser()->BindGpuHostReceiver(...)` (`content/browser/gpu/gpu_process_host_receiver_bindings.cc:52-60`).
3. `ContentBrowserClient::BindGpuHostReceiver()` is specifically documented as handling incoming GPU-process interface binding requests and is called on the IO thread (`content/public/browser/content_browser_client.h:1734-1736`).
4. A CEF/Chromium browser-client hook or content patch would bind the receiver to the browser-owned `QnxGpuHost`.

Pros:

- Directly matches the desired direction: GPU requests a browser-owned interface.
- Does not require a GPU-exposed Ozone service just to pass a host remote.
- Uses Chromium Mojo transport and remains OOP.

Cons / risks:

- Less Ozone-local: requires content/browser or embedder `ContentBrowserClient` glue to find the QNX Ozone singleton and bind `QnxGpuHost`.
- Threading mismatch: the hook is documented as IO-thread, while QNX window/Screen state is browser UI-thread owned. It would need a safe post-to-UI binding path and receiver task runner design.
- Weaker precedent inside Ozone than the Wayland/Flatland `GpuPlatformSupportHost` pattern.
- Harder to keep as a CEF-managed platform-local new-file change.

Likely file touch points:

- GPU-side QNX code that calls `content::ChildThread::Get()->BindHostReceiver(...)` (would introduce a `content` dependency into QNX Ozone GPU code, which is undesirable).
- `content/browser/gpu/gpu_process_host_receiver_bindings.cc` or CEF/Chrome `ContentBrowserClient` receiver-binding files.
- `QnxGpuHost` thread-safe binding support.
- QNX mojom/BUILD files.

### Path C — Extend Viz/GPU host privileged mojom or `GpuHostImpl`

Pattern: make QNX frame submission part of the existing privileged Viz GPU-host channel.

Flow:

1. Add QNX-specific methods or a nested pending remote to a Viz privileged mojom path such as `services/viz/privileged/mojom/gl/gpu_host.mojom` / `GpuHostImpl`.
2. GPU service obtains the Browser-owned receiver/remote during `CreateGpuService()` startup.
3. QNX GPU code calls back through that Viz-level remote.

Pros:

- Tied to GPU process lifetime and `GpuHostImpl` restart handling.
- Clearly browser-owned because `GpuHostImpl` already lives on the browser side.

Cons / risks:

- Most invasive: touches content/viz privileged IPC for a QNX Ozone backend detail.
- Poor layering: QNX Ozone types would leak into Viz or need extra abstraction.
- Higher review/security surface.
- Not needed when Ozone already has the `GpuPlatformSupportHost` launch bridge.

Likely file touch points:

- `services/viz/privileged/mojom/gl/gpu_host.mojom`
- `components/viz/host/gpu_host_impl.{h,cc}`
- `content/browser/gpu/gpu_process_host.*`
- QNX Ozone mojom/source files

## Recommendation for this QNX phase

Use Path A: add a QNX `GpuPlatformSupportHost` connector and invert the current `AddInterfaces()` usage.

Concrete recommendation:

- Keep `QnxGpuHost` Browser-owned.
- Remove/replace direct `binders->Add<qnx::QnxGpuHost>(...)` from `OzonePlatformQnx::AddInterfaces()`.
- Use `AddInterfaces()` only to register a GPU-side QNX control/service receiver, e.g. `QnxGpuControl` or `QnxGpuService`.
- Add an initialization method on the GPU-side interface, e.g. `Initialize(pending_remote<QnxGpuHost> host_remote)`, or introduce a small `QnxGpuService.Initialize(pending_remote<QnxGpuHost>)` and leave `QnxGpuControl` for attach/resize/detach.
- In browser `OnGpuServiceLaunched()`, create/bind the `QnxGpuHost` remote and pass it to the GPU-side service using the supplied `GpuHostBindInterfaceCallback`.
- In browser `OnChannelDestroyed(host_id)`, reset the QNX host receiver/control remote and mark widget records GPU-detached/increment generation.

Why this is minimal and aligned:

- It follows the Wayland pattern already present in Chromium: Browser host interface remote is passed to a GPU-implemented service during Ozone GPU-service launch (`ui/ozone/platform/wayland/host/wayland_buffer_manager_connector.cc:51-65`; `ui/ozone/platform/wayland/mojom/wayland_buffer_manager.mojom:94-116`).
- It uses the existing `GpuPlatformSupportHost` API designed for platform-specific GPU process support (`ui/ozone/public/gpu_platform_support_host.h:25-49`).
- It preserves out-of-process GPU because the browser/GPU pipe is created after GPU service launch via `GpuHostImpl::InitOzone()` (`components/viz/host/gpu_host_impl.cc:435-451`).
- It uses Mojo platform handles for DMAbuf fds as already specified by the QNX design (`docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md:419-428`, `:430-440`) and does not add a custom Unix socket.

## Exact files likely to change in the next implementation substep

Minimum expected set:

1. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`
   - Add GPU-side initialization surface: either `QnxGpuControl.Initialize(pending_remote<QnxGpuHost> host)` or a new `QnxGpuService` interface with that method.
2. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`
   - Return QNX connector from `GetGpuPlatformSupportHost()`.
   - Create connector in `InitializeUI()`.
   - Change `AddInterfaces()` to register only the GPU-side control/service binding.
3. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.h`
4. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_host.cc`
   - Add a `BindInterface()` helper returning `mojo::PendingRemote<qnx::QnxGpuHost>` or equivalent.
   - Add reset/disconnect behavior for GPU restart.
5. New `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.h`
6. New `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_platform_support_host.cc`
   - Implements `ui::GpuPlatformSupportHost` and owns the launch/restart bridge.
7. New `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_gpu_control.h` and `.cc` or `qnx_gpu_service.h` and `.cc`
   - GPU-side receiver bound from `AddInterfaces()`; stores `mojo::Remote<QnxGpuHost>`.
8. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.h`
9. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window_manager.cc`
   - Only if a widget enumeration or atomic attach/generation helper is needed for initial attach/reconnect.
10. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
    - Add new connector/control/service sources.
11. `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/mojom/BUILD.gn`
    - Only if the mojom is split into an additional file; not needed if `qnx_gpu.mojom` remains the sole source.

## Validation commands for next implementation

Compile-level validation:

```sh
cd /home/yuta/chromium/src
QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  gn gen out/qnx_phase5_mojo_binding --args="$(cat out/qnx_release/args.gn; echo ozone_platform_qnx=true)"

QNX_HOST=/home/yuta/qnx800/host/linux/x86_64 \
QNX_TARGET=/home/yuta/qnx800/target/qnx \
  ninja -C out/qnx_phase5_mojo_binding \
    ui/ozone/platform/qnx/mojom:mojom \
    ui/ozone/platform/qnx:qnx
```

CEF-managed clean-tree validation before durable acceptance:

```sh
cd /home/yuta/chromium/src
./cef/tools/qnx_sync_sources.sh -f -R
./cef/tools/cef_create_projects_qnx.sh --build-type Release --qnx-sdp-root /home/yuta/qnx800
./out/qnx_release/ninja_qnx.sh ui/ozone/platform/qnx:qnx
```

Runtime/log smoke once the compile path exists:

```sh
cd /home/yuta/chromium/src
./out/qnx_release/ninja_qnx.sh cefclient -k 20 2>&1 | tee build.log
# If the log is large, parse with ned rather than reading raw build.log:
.venv/bin/ned parse build.log --format json > build.summary.json
```

Expected implementation-specific log evidence for a later runtime smoke:

- Browser: `QnxGpuPlatformSupportHost::OnGpuServiceLaunched(host_id=...)` ran.
- Browser: `QnxGpuHost` receiver bound in the browser process.
- GPU: `QnxGpuControl`/`QnxGpuService` receiver bound via Ozone `AddInterfaces()`.
- GPU: stored `mojo::Remote<QnxGpuHost>` after `Initialize(...)`.
- Browser: `OnChannelDestroyed(host_id=...)` runs on GPU process exit/restart and resets QNX GPU attachment state.

## Explicit non-goals for the next substep

- Do not implement EGL import/display until the browser-owned `QnxGpuHost` receiver is correctly bound and reconnect-safe.
- Do not accept `--in-process-gpu` as validation for this binding path.
- Do not add a custom Unix-domain socket; use Mojo `handle<platform>` for DMAbuf fd transport.
- Do not pass `screen_window_t` or raw Screen pointers across processes.
- Do not migrate to Chromium-wide `gfx::NativePixmapHandle`/`GpuMemoryBufferHandle` serialization in this substep.
- Do not broaden into rendering correctness; first prove process direction, pipe binding, disconnect/reconnect, and metadata-only frame call plumbing.

## Blockers / user decisions

- Blocker: the current direct `AddInterfaces()` registration of `QnxGpuHost` should not be implemented further; it is the wrong process direction for OOP GPU.
- Decision needed before coding: name the GPU-side startup interface. Two acceptable choices:
  - extend current `QnxGpuControl` with `Initialize(pending_remote<QnxGpuHost> host_remote)`, or
  - introduce `QnxGpuService.Initialize(pending_remote<QnxGpuHost> host_remote)` and keep `QnxGpuControl` limited to widget lifecycle.
- No external feasibility blocker found for using Mojo rather than sockets; this path is exactly the existing Ozone browser/GPU launch bridge.

## Residual risks

- Receiver lifetime on crash/restart must be designed carefully. A single `mojo::Receiver` can work if the connector always resets it before rebinding; otherwise use `mojo::ReceiverSet`.
- Widget attach/generation semantics are not currently sufficient for real frame acceptance. Generation starts at zero and `gpu_attached` starts false (`qnx_window_manager.h:29-41`); binding implementation must initialize generation/attachment before expecting `SubmitFrame()` success.
- Thread affinity must remain browser UI thread for Screen/window state. This is one reason Path A is preferable to the IO-thread `BindGpuHostReceiver` path.
- A runtime smoke will still need a GPU-side caller for metadata-only `SubmitFrame()`; compile success alone proves the binding compiles, not that the pipe is exercised.

```acceptance-report
{
  "criteriaSatisfied": [
    {
      "id": "criterion-1",
      "status": "satisfied",
      "evidence": "Concrete findings cite QNX files and Chromium call paths: ozone_platform_qnx.cc:176-193, content/gpu/gpu_child_thread.cc:213-223, content/gpu/browser_exposed_gpu_interfaces.cc:18-30, components/viz/host/gpu_host_impl.cc:435-451, and Wayland/DRM Ozone precedents."
    }
  ],
  "changedFiles": [
    "docs/qnx/history/research/qnx-ozone-phase5-mojo-binding-path-design-2026-07-03.md"
  ],
  "testsAddedOrUpdated": [],
  "commandsRun": [
    {
      "command": "read/grep inspections of required QNX docs and QNX Ozone new_files",
      "result": "passed",
      "summary": "Inspected plan/design/audit/compile-fix docs plus qnx_gpu.mojom, qnx_gpu_host.{cc,h}, and ozone_platform_qnx.cc."
    },
    {
      "command": "grep/read inspections of Chromium GPU/Ozone binding paths",
      "result": "passed",
      "summary": "Inspected browser_exposed_gpu_interfaces.cc, gpu_child_thread.cc, GpuHostImpl, GpuPlatformSupportHost, Wayland/DRM/Flatland Ozone patterns, and BindGpuHostReceiver path."
    },
    {
      "command": "nl -ba ... | sed -n ... for cited files",
      "result": "passed",
      "summary": "Collected line-number evidence for all cited QNX/Chromium binding-path files."
    },
    {
      "command": "git diff --cached --name-only && git status --short <scoped paths>",
      "result": "passed",
      "summary": "Confirmed no staged files; scoped status showed this report as untracked and the pre-existing plan/source paths also untracked."
    }
  ],
  "validationOutput": [
    "Design-only investigation; no build/test validation was run.",
    "Confirmed AddInterfaces is invoked from GPU child browser-exposed-interface path and should not directly bind browser-owned QnxGpuHost."
  ],
  "residualRisks": [
    "Need implementation decision: extend QnxGpuControl with Initialize(pending_remote<QnxGpuHost>) or add QnxGpuService.",
    "Receiver reset/ReceiverSet policy must be implemented for GPU restart.",
    "Widget generation/attach initialization remains required before real SubmitFrame acceptance."
  ],
  "noStagedFiles": true,
  "diffSummary": "Added the requested design report only; no source files or ozone-out-of-process-gpu-plan.md edits.",
  "reviewFindings": [
    "blocker: patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc:176-193 - direct QnxGpuHost registration in Ozone AddInterfaces has the wrong process direction for out-of-process GPU.",
    "note: content/gpu/gpu_child_thread.cc:213-223 and content/gpu/browser_exposed_gpu_interfaces.cc:18-30 show AddInterfaces is populated in the GPU child browser-exposed-interface path.",
    "note: recommended path is QNX GpuPlatformSupportHost bridge modeled on Wayland connector, passing a browser-owned QnxGpuHost pending_remote to a GPU-side QnxGpuControl/QnxGpuService."
  ],
  "manualNotes": "Respected design-only/no-source-edit instruction; wrote only this report."
}
```
