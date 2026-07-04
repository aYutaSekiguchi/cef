# QNX Ozone out-of-process GPU implementation plan

> Created: 2026-07-02
> Status: Phase 5 content_shell now builds; QNX runtime reaches browser startup, renderer init, and GPU process stays alive (no crash/restart loop) with QNX Ozone + OOP GPU selected. GPU Mojo/Viz init chain confirmed through OnGpuServiceConnection. Next: verify QNX GPU trace handoff (QnxGpuPlatformSupportHost::OnGpuServiceLaunched) and SubmitFrame smoke. (2026-07-04).
> Scope: Native QNX Screen/EGL Ozone backend for Chromium/CEF, targeting x86_64 QEMU first and aarch64 boards later.

## Operating rule

This file is the durable source of truth for this work. Before starting any new phase or newly discovered task, update this document first. After each phase completes, mark its checklist item complete and record evidence. Do not rely on conversation context for the plan.

## Current runtime / machine-safety note

As of 2026-07-04, `content/shell:content_shell` builds successfully for QNX with `-j10`; latest successful build log: `out/qnx_release/content_shell_recovery_build72_skiarend.log`. The binary needed two runtime-loader mitigations on QNX x86_64: SysV ELF hash tables and non-PIE executable links. With `ozone_platform_qnx = true`, `content_shell --ozone-platform=qnx --use-gl=egl --no-sandbox --ozone-qnx-gpu-trace about:blank` now reaches browser startup, spawns a stable GPU process, initializes a renderer process, and remains alive (no GPU crash/restart loop). Latest smoke log: `out/qnx_release/content_shell_qnx_virgl_smoke29_skiarend.log`.

Current next blocker (resolved): Three NOTREACHED/int3 crashes in the GPU-process rendering pipeline were fixed:

1. `QnxGLOzoneEGL::CreateViewGLSurface` → falls back to offscreen pbuffer (`ui_ozone_qnx_gl_ozone_egl_view_to_offscreen`, build69).
2. `PbufferGLSurfaceEGL::SwapBuffers` → QNX no-op returning `SWAP_ACK` (`ui_gl_gl_surface_egl_pbuffer_swap_noop_qnx`, build71).
3. `SkiaRenderer::BuffersPresented` / `DidReceiveReleasedOverlays` → QNX no-op (`viz_skia_renderer_noop_buffers_presented_qnx`, build72).

Current next blocker (active):

- Browser process starts with native qnx Ozone and receives QNX Screen events.
- GPU process is launched out-of-process, and all three NOTREACHED crash points in the pbuffer-offscreen swap/present/release path are resolved (build69-72).
- GPU process now survives indefinitely (no crash/restart loop in smoke29).
- GPU Mojo/Viz init chain confirmed through OnGpuServiceConnection (signal handlers installed).
- Renderer process (pid 610339) initializes successfully.
- Next work should verify QNX GPU trace handoff (`QnxGpuPlatformSupportHost::OnGpuServiceLaunched`, `QnxGpuService::Initialize`) and SubmitFrame smoke, then clean up debug LOG statements.

Safety note: large broad builds are now user-approved at `-j10`, but continue to capture logs to `out/qnx_release/*.log` and summarize them; do not read raw logs directly.

## User-approved direction

The final implementation should aim for the complete architecture from the start:

- Browser/UI process owns visible QNX Screen windows and input/window lifecycle.
- GPU process owns render producer resources and can crash/restart independently.
- `--in-process-gpu` is allowed only as a feasibility-study or debugging tool, not as the final architecture or final acceptance path.
- Existing headless QNX test paths must remain working.

## Non-goals / guardrails

- Do not treat `--in-process-gpu` as Phase 1 product architecture.
- Do not pass raw `screen_window_t` pointers across processes as `gfx::AcceleratedWidget`.
- Do not introduce Wayland/Weston, DRM/GBM, GTK, or Qt as first-class dependencies unless this plan is updated and approved.
- Keep durable QNX changes under CEF-managed locations:
  - patches: `patch/patches/qnx/` and `patch/patches/qnx/chromium/`
  - new Chromium files: `patch/qnx/chromium/new_files/`
  - docs: `docs/qnx/`
- Preserve default headless behavior unless explicitly running `--ozone-platform=qnx`.

## Target architecture

```text
Browser/UI process
  - owns visible screen_window_t
  - handles input / bounds / visibility / close / activation
  - exposes stable gfx::AcceleratedWidget numeric IDs
  - consumes or displays GPU-produced buffers/streams
        |
        | Mojo handle<platform> carrying DMAbuf fd(s) + EGLImage import
        v
GPU process
  - owns render producer resources
  - creates EGL/GLES render target
  - renders frames and posts/swaps them
  - may crash and be restarted without destroying the browser-visible window
```

### Core architectural decisions

- `gfx::AcceleratedWidget` on QNX is a stable numeric widget ID, not a `screen_window_t` pointer.
- Browser-side `QnxWindow` owns the visible `screen_window_t`.
- GPU-side QNX code maps widget ID + generation + size to a render producer.
- GPU restart must recreate only GPU-side producer resources and reconnect them to the browser-visible window.

## Open feasibility questions

These must be answered before final Ozone implementation is committed:

1. Which QNX/EGL stream mechanisms are actually available at runtime in QEMU virgl?
   - `EGL_KHR_stream`: **absent in QEMU virgl / Mesa** (Phase 1A)
   - `EGL_KHR_stream_producer_eglsurface`: **absent in QEMU virgl / Mesa** (Phase 1A)
   - `EGL_KHR_stream_cross_process_fd`: **absent in QEMU virgl / Mesa** (Phase 1A)
   - `EGL_QNX_image_native_buffer`: **present** (Phase 1A)
   - `GL_OES_EGL_image`: **present** (Phase 1A)
2. Can a producer process create an EGL/GLES render stream/target that a separate browser-like consumer process displays via Screen APIs?
   - **Blocked in QEMU virgl for EGL_KHR_stream** because the stream extension family is absent.
3. Can the consumer survive producer crash and reconnect after producer restart?
   - Pending; depends on selected sharing primitive.
4. If Screen streams are insufficient for final browser-visible composition, what is the next-best QNX-native sharing primitive?
   - **Selected for Phase 1B: DMAbuf/EGLImage probe in QEMU virgl.** User approved the recommended DMAbuf probe path on 2026-07-02 after Phase 1A showed absent EGL streams. Evidence available: `EGL_MESA_image_dma_buf_export`, `EGL_EXT_image_dma_buf_import`, `EGL_EXT_image_dma_buf_import_modifiers`, `EGL_QNX_image_native_buffer`, and `GL_OES_EGL_image`.

## Phase checklist

### Phase 0 — Plan persistence

- [x] Create this durable plan file.
- [ ] Commit or otherwise preserve this plan file after review, if requested.

Evidence:

- Plan file path: `docs/qnx/ozone-out-of-process-gpu-plan.md`

### Phase 1 — Standalone feasibility probes

Delegation status: Phase 1A extension inventory probe is being delegated to subagents. The parent/orchestrator owns this plan file; subagents must not edit it directly. If a subagent discovers a necessary new task or scope change, it must report it and stop before doing that new work so the orchestrator can update this plan first.

Delegation attempts:

- 2026-07-02: async worker run `1a4c0fad-653e-4801-8fac-5ba30797a8fd` failed before writing a result (`stale-run reconciliation`; no child session persisted). No probe work was accepted from that run. Retried with a foreground worker.
- 2026-07-02: foreground worker completed Phase 1A and wrote `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1a-2026-07-02.md`.
- 2026-07-02: foreground worker for Phase 1B DMAbuf probe timed out after 20 minutes before writing the required report. It left partial/unaccepted probe files under `tools/qnx_probes/` (`qnx_dmabuf_ipc.h`, `qnx_dmabuf_export_producer.c`, `qnx_dmabuf_import_consumer.c`). A broad recovery worker was delegated next.
- 2026-07-02: broad Phase 1B recovery worker timed out after 30 minutes before writing the required report. It may have further modified the same partial/unaccepted probe files. Next action: stop broad implementation delegation and split recovery into microtasks. First microtask is read-only audit of partial files and attempted approach; it must write an audit report only. After the orchestrator updates this plan from that audit, a separate small worker may be delegated for one concrete compile/run milestone.

Planned/created probe files:

- `tools/qnx_probes/README.md` — created in Phase 1A
- `tools/qnx_probes/qnx_egl_extension_probe.c` — created in Phase 1A
- `tools/qnx_probes/qnx_dmabuf_ipc.h` — approved for Phase 1B shared probe helpers
- `tools/qnx_probes/qnx_dmabuf_export_producer.c` — approved for Phase 1B DMAbuf/EGLImage producer
- `tools/qnx_probes/qnx_dmabuf_import_consumer.c` — approved for Phase 1B DMAbuf/EGLImage consumer
- `tools/qnx_probes/qnx_scmrights_probe.c` — approved for Phase 1B-scmrights / Phase 1B-devicefd prerequisite probe
- `tools/qnx_probes/qnx_dmabuf_export_only_probe.c` — approved for Phase 1B-exportonly true DMAbuf export isolation
- `tools/qnx_probes/qnx_screen_egl_window_probe.c` — approved for Phase 1B-display-isolation minimal Screen/EGL window-surface probe
- `tools/qnx_probes/qnx_dmabuf_restart_consumer.c` — approved for Phase 1B-crash browser-like restart consumer
- `tools/qnx_probes/qnx_dmabuf_restart_producer.c` — approved for Phase 1B-crash producer process
- The original stream names `qnx_screen_stream_consumer.c` and `qnx_egl_stream_producer.c` are deferred until real-hardware EGL stream validation is explicitly scheduled.

Phase 1A build/run commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_egl_extension_probe \
  tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl -- ./qnx_egl_extension_probe
```

Phase 1B initial build/run commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c -lsocket -lscreen -lEGL -lGLESv2
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c -lsocket -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock /tmp/qnx-dmabuf-consumer.bmp; ./qnx_dmabuf_import_consumer & sleep 1; ./qnx_dmabuf_export_producer; wait'
```

The worker may adjust command quoting or sequencing if required by the QNX shell, but must not add new source files or change runner/GN code without reporting back for a plan update first.

Phase 1B recovery split after two broad worker timeouts:

- Phase 1B-audit: completed by read-only reviewer report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md`.
- Phase 1B-smoke: bounded runtime smoke of the current partial producer/consumer files. This completed and validated only socket/raw-pixel transfer plus GL texture creation; it is **not** accepted as DMAbuf evidence. It also found `SCREEN_PROPERTY_EGL_HANDLE` failure in the consumer Screen-window composition path.
- Phase 1B-scmrights: validate QNX Unix-domain `SCM_RIGHTS` fd passing independently of EGL/Screen. This completed with mixed results: pipe fd passing works, but regular-file fd passing produced an unusable received fd (`read`/`pread` returned `EBADF`) despite valid ancillary metadata.
- Phase 1B-devicefd: validate `SCM_RIGHTS` with a character-device fd. This completed successfully with `/dev/null` opened `O_RDWR`; the received character-device fd preserved access mode and remained usable.
- Phase 1B-exportonly: completed. Hardened default Path A (`eglCreateDRMImageMESA`) exported one real DMAbuf fd under QEMU virgl via `eglExportDMABUFImageMESA` without pbuffer fallback.
- Phase 1B-dmabuf-import: completed. True DMAbuf export/fd-pass/import/bind works across separate producer/consumer processes in QEMU virgl. Screen display/composition was intentionally skipped.
- Phase 1B-display-isolation: completed. A minimal Screen window can be used directly as an EGL window surface under QEMU virgl; `SCREEN_PROPERTY_EGL_HANDLE` fails but is not required.
- Phase 1B-display: completed. Imported DMAbuf texture was rendered to a visible Screen/EGL window surface and swapped successfully under QEMU virgl.
- Phase 1B-crash: completed. Standalone restart probe aligned with final architecture: browser-like consumer owned one visible Screen window and listening socket, accepted one producer that exited non-zero after sending a frame, kept the window alive, then accepted a second producer and rendered again.
- Phase 2 design update: next. Selected primitive is DMAbuf/EGLImage with `SCM_RIGHTS` fd passing. Browser/UI owns visible Screen window and imports/displays GPU-produced DMAbuf frames; GPU process owns export/render producer resources.

Current partial `qnx_dmabuf_*` files are **not accepted as Phase 1B DMAbuf evidence** because the audit and smoke reports show the producer currently sets `hdr.exported = 2`, `n_planes = 0`, and uses a raw RGBA socket-transfer fallback instead of exporting/passing a DMAbuf fd.

Approved probe files for Phase 1B prerequisite/export/display-isolation microtasks:

- `tools/qnx_probes/qnx_scmrights_probe.c`
- `tools/qnx_probes/qnx_dmabuf_export_only_probe.c`
- `tools/qnx_probes/qnx_screen_egl_window_probe.c`

Initial Phase 1B-scmrights / Phase 1B-devicefd commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_scmrights_probe \
  tools/qnx_probes/qnx_scmrights_probe.c -lsocket
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_scmrights_probe
```

Initial Phase 1B-exportonly commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_export_only_probe \
  tools/qnx_probes/qnx_dmabuf_export_only_probe.c -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_dmabuf_export_only_probe
```

Phase 1B-exportonly stop conditions:

- Stop/report if required EGL extensions or function pointers for the chosen non-pbuffer path are absent.
- Stop/report if pbuffer is the only available path; do not re-enter the known Mesa/QNX virgl pbuffer crash path unless the probe can guard it safely.
- Stop/report immediately after the first successful export of at least one DMAbuf fd, including format/stride/modifier metadata; do not broaden into IPC/import/display in this microtask.

Initial Phase 1B-display-isolation commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_screen_egl_window_probe \
  tools/qnx_probes/qnx_screen_egl_window_probe.c -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_screen_egl_window_probe
```

Phase 1B-display-isolation stop conditions:

- Stop/report if `screen_create_window()` or basic Screen window setup fails.
- Test and report `SCREEN_PROPERTY_EGL_HANDLE` availability, but also test whether `eglCreateWindowSurface(display, config, (EGLNativeWindowType)screen_window_t, NULL)` works directly without querying the property.
- If an EGL window surface can be created, clear/post one color with `eglSwapBuffers()` and report success. Screenshot capture is optional.
- Do not combine this with DMAbuf import/display or crash/restart in the same microtask.

Initial Phase 1B-crash commands:

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_restart_consumer \
  tools/qnx_probes/qnx_dmabuf_restart_consumer.c -lsocket -lscreen -lEGL -lGLESv2
qcc -Wall -Wextra -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_dmabuf_restart_producer \
  tools/qnx_probes/qnx_dmabuf_restart_producer.c -lsocket -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_restart.sock; ./qnx_dmabuf_restart_consumer & sleep 1; ./qnx_dmabuf_restart_producer --exit-code=37; first=$?; ./qnx_dmabuf_restart_producer --exit-code=0; second=$?; wait; echo restart exits: $first $second'
```

Phase 1B-crash acceptance:

- Consumer owns one visible Screen window for the whole run and logs a stable window/context identity across both producer connections.
- First producer sends a valid DMAbuf frame and exits non-zero after the consumer has imported/displayed it; the consumer remains alive.
- Second producer sends a valid DMAbuf frame and exits zero; consumer imports/displays it on the same window and exits cleanly.
- Do not implement Chromium/Ozone code or broad recovery policy in this standalone probe.

- [x] Add/update this plan with exact probe filenames and initial commands before writing probe code.
- [x] Build and run EGL/Screen extension inventory probe under `./tools/qnx_run.sh --virgl`.
- [x] Decide the Phase 1B sharing primitive before writing more probe code.
- [x] Audit partial Phase 1B files after broad worker timeouts.
- [x] Run bounded runtime smoke of current partial producer/consumer files and classify result as raw-pixel/socket smoke or blocker, not DMAbuf evidence.
- [x] Build and run SCM_RIGHTS fd-passing prerequisite probe.
- [x] Extend/run SCM_RIGHTS probe for character-device fd behavior.
- [x] Harden export-only true DMAbuf probe source and compile without entering runtime.
- [x] Build and run export-only true DMAbuf probe before full producer/consumer IPC.
- [x] Fix producer parent-to-consumer header+SCM_RIGHTS framing and compile producer/consumer.
- [x] Build and run true DMAbuf/EGLImage producer/consumer import/bind probe for the selected primitive, without Screen display.
- [x] Build and run minimal Screen/EGL window-surface display-isolation probe.
- [x] Build and run imported-texture Screen display/composition probe if display-isolation is viable.
- [x] Simulate producer crash/restart and confirm the consumer-visible window remains alive.
- [x] Record Phase 1A logs/results in `docs/qnx/history/research/`.

Acceptance evidence:

- Phase 1A report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1a-2026-07-02.md`
- Phase 1B audit report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-audit-2026-07-02.md`
- Phase 1B smoke report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-smoke-2026-07-02.md`
- Phase 1B SCM_RIGHTS report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-scmrights-2026-07-02.md`
- Phase 1B device-fd report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-devicefd-2026-07-02.md`
- Phase 1B export-only audit report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-audit-2026-07-02.md`
- Phase 1B export-only hardening report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-hardening-2026-07-02.md`
- Phase 1B export-only runtime report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-runtime-2026-07-02.md`
- Phase 1B DMAbuf import audit report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-audit-2026-07-02.md`
- Phase 1B DMAbuf import framing report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-framing-2026-07-02.md`
- Phase 1B DMAbuf import runtime report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-runtime-2026-07-02.md`
- Phase 1B display-isolation report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-isolation-2026-07-02.md`
- Phase 1B display report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-2026-07-02.md`
- Phase 1B crash/restart report: `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-crash-2026-07-02.md`
- Probe source locations: `tools/qnx_probes/README.md`, `tools/qnx_probes/qnx_egl_extension_probe.c`
- Commands used:
  - `qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_egl_extension_probe tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2`
  - `./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_egl_extension_probe`
- QEMU output summary: Screen context and EGL 1.5 initialized successfully under Mesa/virgl.
- Phase 1A decision: EGL stream path is **not viable in QEMU virgl** because `EGL_KHR_stream`, `EGL_KHR_stream_producer_eglsurface`, and `EGL_KHR_stream_cross_process_fd` are absent. Alternate primitive selection is required before Phase 1B.
- Phase 1B smoke decision: current partial producer/consumer files produce raw-pixel/socket smoke evidence only. No true DMAbuf evidence was produced; `eglExportDMABUFImageMESA` was not called, no fd was sent, and `SCREEN_PROPERTY_EGL_HANDLE` failed during Screen-window composition.
- Phase 1B SCM_RIGHTS decision: true fd passing was proven for pipe fds. Regular-file fd passing is suspect on QNX/QEMU because the received fd has valid metadata but is unusable for `read`/`pread` (`EBADF`). Character-device fd behavior was tested next and passed, which strongly suggests DMAbuf/PRIME fd passing is viable if a real DMAbuf fd can be exported.
- Phase 1B export-only audit decision: current `qnx_dmabuf_export_only_probe.c` had compile-only evidence, not runtime/export evidence. It was hardened before QEMU runtime because its non-pbuffer Path A attribute list was likely malformed and it could fall into a known-risk pbuffer fallback.
- Phase 1B export-only hardening/runtime decision: default runtime path avoids pbuffer fallback and successfully exported a real DMAbuf fd under QEMU virgl (`AR24`, one plane, stride 256, modifier 0, fd flags read/write). `--allow-pbuffer-risk` remains unauthorized.
- Phase 1B next decision: update the producer/consumer probe to reuse the proven export-only Path A and validate fd passing plus EGL import/bind. Do not attempt Screen-window display in this next microtask.
- Phase 1B DMAbuf import decision: producer/consumer import/bind milestone passed. The complete chain GPU-like producer process -> SCM_RIGHTS fd passing -> browser-like consumer process -> EGLImage -> GL texture is proven under QEMU virgl.
- Phase 1B display-isolation decision: `SCREEN_PROPERTY_EGL_HANDLE` failure is non-fatal. Direct `eglCreateWindowSurface(display, config, (EGLNativeWindowType)screen_window_t, NULL)` succeeds, and `eglSwapBuffers()` works under QEMU virgl.
- Phase 1B display decision: imported DMAbuf texture -> EGLImage -> GL texture -> visible Screen/EGL window surface -> `eglSwapBuffers()` passed under QEMU virgl.
- Phase 1B crash/restart decision: browser-like consumer kept one stable `screen_window_t` and EGLSurface alive across non-zero producer exit and second producer reconnect. The complete QEMU virgl feasibility chain passed. Selected primitive for Phase 2 is DMAbuf/EGLImage; production IPC will use Chromium Mojo `handle<platform>` while the Phase 1B `SCM_RIGHTS` probes remain underlying fd-transfer evidence.

### Phase 2 — Final backend design update

Selected sharing primitive: DMAbuf/EGLImage. Production Chromium IPC uses Mojo `handle<platform>` for DMAbuf fd transport; raw `SCM_RIGHTS` was probe-only evidence of QNX fd-passing viability.

Planned design note path:

- `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`

Required design note contents:

- Browser/UI process owns visible `screen_window_t`, stable widget IDs, input/window lifecycle, and Screen/EGL display surface.
- GPU process owns export/render producer resources and emits DMAbuf frame fds plus metadata.
- Browser/GPU handshake maps stable widget ID + generation + size to a producer connection through QNX-local Mojo interfaces.
- Browser imports DMAbuf via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` and displays via direct `eglCreateWindowSurface(screen_window_t)`.
- GPU crash/restart preserves browser-owned Screen window and reconnects with a new generation.
- Headless QNX paths remain default and unaffected unless `--ozone-platform=qnx` is selected.

- [x] Update this plan with the selected sharing primitive.
- [x] Update this plan with final class responsibilities after design note is written.
- [x] Write detailed design note under `docs/qnx/`.
- [x] Get user approval before implementing Chromium/Ozone backend files. User approved starting implementation after the Mojo IPC design revision.

Final class responsibilities are documented in `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`, section 5. Summary:

- Browser/UI classes: `OzonePlatformQnx`, `QnxScreenContext`, `QnxWindowManager`, `QnxWindow`, `QnxPlatformEventSource`.
- GPU classes: `QnxSurfaceFactoryOzone`, `QnxGLOzoneEGL`, `QnxRenderProducer`, `QnxGLES2Surface`.
- Shared/common: `QnxDmaBufFrame`, `ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`.

Acceptance evidence:

- Design note path: `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`
- User approval reference.

### Phase 3 — GN/Ozone wiring only

Status: complete. Additive GN/Ozone registration is in place with minimal compile-only stubs; no Screen/EGL runtime backend logic was added in this phase.

- [x] Add `ozone_platform_qnx` wiring in CEF-managed patches.
- [x] Fix Phase 3 stub compile blocker found by review.
- [x] Keep default headless path unchanged.
- [x] Verify `gn gen` succeeds.
- [x] Verify existing headless target selection remains available.

Acceptance evidence:

- Patch files changed:
  - `patch/patches/qnx/chromium/ozone_platform_qnx_build.gni.patch`
  - `patch/patches/qnx/chromium/ozone_build_qnx_platform.gni.patch`
  - `patch/patch.cfg`
- New files:
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/BUILD.gn`
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.{cc,h}`
  - `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.{cc,h}`
- Reports:
  - `docs/qnx/history/research/qnx-ozone-phase3-gn-wiring-2026-07-03.md`
  - `docs/qnx/history/research/qnx-ozone-phase3-review-2026-07-03.md`
  - `docs/qnx/history/research/qnx-ozone-phase3-stub-fix-2026-07-03.md`
  - `docs/qnx/history/research/qnx-ozone-phase3-validation-2026-07-03.md`
- Validation:
  - `gn gen out/qnx_phase3` with `ozone_platform_qnx=true` succeeded.
  - `ninja -C out/qnx_phase3 ui/ozone/platform/qnx:qnx` succeeded.
  - `git diff --check` and patch dry-runs passed.

### Phase 4 — Browser/UI-side QNX Ozone skeleton

Status: complete for the minimal Browser/UI skeleton. The Screen skeleton compiles, `InitializeUI()` returns true after Screen context/window manager setup, and a minimal timer-polled `QnxPlatformEventSource` is wired. Full event translation, real display enumeration, and Mojo frame transport remain deferred subwork.

Planned files, subject to Phase 2 design confirmation:

- `ui/ozone/platform/qnx/BUILD.gn`
- `ui/ozone/platform/qnx/ozone_platform_qnx.{cc,h}`
- `ui/ozone/platform/qnx/qnx_screen_context.{cc,h}`
- `ui/ozone/platform/qnx/qnx_window_manager.{cc,h}`
- `ui/ozone/platform/qnx/qnx_window.{cc,h}`
- `ui/ozone/platform/qnx/qnx_platform_event_source.{cc,h}`
- `ui/ozone/platform/qnx/qnx_screen.{cc,h}`
- `ui/ozone/platform/qnx/mojom/qnx_gpu.mojom` (may be deferred to the Phase 4 Mojo substep if the first Screen skeleton can compile without it)

Responsibilities:

- [x] visible `screen_window_t` creation/destruction
- [x] stable `AcceleratedWidget` allocation
- [x] minimal input/event source skeleton for smoke bring-up; full key/pointer/touch translation deferred
- [x] bounds/resize/visibility tracking
- [ ] producer attach/detach hooks over QNX-local Mojo interfaces (can remain stubbed until later Phase 4/5 substep)

Acceptance evidence:

- `ozone_platform_qnx=true` build reaches compile/link for relevant targets.
- First substep report: `docs/qnx/history/research/qnx-ozone-phase4-screen-skeleton-2026-07-03.md`
- Event source report: `docs/qnx/history/research/qnx-ozone-phase4-event-source-2026-07-03.md`
- Minimal `OzonePlatformQnx::InitializeUI()` returns true after Screen context/window manager setup.
- `ninja -C out/qnx_phase4_event ui/ozone/platform/qnx:qnx` succeeded.
- No regression to default headless behavior.

### Phase 5 — GPU-side QNX render producer

Status: active but paused for safety. QNX-local Mojo interface, GPU-side producer/export scaffold, browser/GPU Mojo service binding, attach/generation lifecycle, GPU-side render-producer lifecycle, browser-side EGL/Screen import/display scaffold, attach-time SubmitFrame trigger, and bounded `ozone_demo` software-canvas smoke compile/run after validation. The accepted binding architecture uses the Ozone `GpuPlatformSupportHost` launch bridge: browser owns `QnxGpuHost`, GPU exposes startup `QnxGpuService`, and browser passes a `pending_remote<QnxGpuHost>` to the GPU service after launch. OOP smoke target audit completed; `content_shell` is the smallest viable OOP candidate but is large/heavy; no broad build without explicit approval. `--ozone-qnx-gpu-trace` diagnostic switch implemented. Next: narrow compile validation of the trace logging changes at `-j10`; broad `content_shell` build/run with `--ozone-qnx-gpu-trace` requires explicit approval.

Planned files, subject to Phase 2 design confirmation:

- `ui/ozone/platform/qnx/qnx_surface_factory.{cc,h}`
- `ui/ozone/platform/qnx/qnx_gl_ozone_egl.{cc,h}`
- `ui/ozone/platform/qnx/qnx_render_producer.{cc,h}`
- `ui/ozone/platform/qnx/qnx_gl_surface.{cc,h}` or equivalent
- `ui/ozone/platform/qnx/client_native_pixmap_factory_qnx.{cc,h}`
- `ui/ozone/platform/qnx/mojom/qnx_gpu.mojom`

Responsibilities:

- define QNX-local Mojo messages carrying DMAbuf `handle<platform>` planes
- create EGL/GLES render producer per widget ID/generation
- manage resize/recreate
- post/swap frames
- cleanly recover when GPU process restarts

Acceptance evidence:

- [x] QNX Mojo mojom uses `gfx.mojom.AcceleratedWidget` for widget IDs, generates, and compiles.
- [x] QNX mojom target layout has no duplicate/conflicting generated outputs.
- [x] GPU-side producer/export scaffold compiles with `QnxRenderProducer`, `QnxSurfaceFactoryOzone`, and `QnxGLOzoneEGL`.
- [x] Browser-side QNX Mojo host scaffold compiles after fixing `NOTREACHED_IN_MIGRATION` and EGL include/type ordering.
- [x] Browser-owned `QnxGpuHost` receiver binding path is designed: use QNX `GpuPlatformSupportHost` plus GPU-side `QnxGpuService.Initialize(pending_remote<QnxGpuHost>)`, not direct browser-host registration in `AddInterfaces`.
- [x] Browser-owned `QnxGpuHost` receiver binding path is implemented and compiles with `QnxGpuService` startup plumbing.
- [x] Attach/generation lifecycle plumbing exists so widgets move from detached generation 0 to a valid GPU-attached generation before metadata-only frame submission.
- [x] GPU-side `QnxGpuService` creates/resizes/destroys `QnxRenderProducer` instances on attach/resize/detach and can exercise metadata-only or export-only `SubmitFrame`.
- [x] Browser-side `QnxGpuHost::SubmitFrame` has a compiled DMAbuf import/display scaffold through the browser-owned Screen/EGL window surface.
- [x] Attach-time out-of-process GPU path has a compiled trigger that attempts to create GPU-side render resources and submit one frame through Mojo.
- [x] QNX Ozone `ozone_demo --ozone-platform=qnx` starts without the earlier keyboard-layout-engine segfault.
- [x] QEMU virgl `ozone_demo --ozone-platform=qnx` software-canvas smoke runs without startup crash or software-surface failure and captures a screenshot artifact.
- [ ] QEMU virgl runtime smoke demonstrates at least one out-of-process `SubmitFrame` attempt and records log/screenshot evidence.
- [x] Identify smallest viable out-of-process GPU smoke target: `content_shell` is the only candidate found; `ozone_demo` uses `single_process=true` so it never exercises OOP GPU path.
- [x] Add `--ozone-qnx-gpu-trace` diagnostic command-line switch that emits grep-stable `QNX_OZONE_GPU_TRACE` prefix logs at key Mojo IPC boundaries in `QnxGpuService::Initialize`, `AttachWidget`, `SubmitTestFrameForWidget`, and `QnxGpuHost::SubmitFrame`. Existing behavior is unchanged when the switch is absent.
- No final acceptance depends on `--in-process-gpu`.

### Phase 6 — Browser/GPU reconnect and crash recovery

- [ ] Implement or wire Mojo browser/GPU handshake for widget ID + generation + size.
- [ ] Verify producer death does not destroy browser-visible window.
- [ ] Verify producer restart reconnects and resumes drawing.
- [ ] Document recovery behavior and limitations.

Acceptance evidence:

- Crash/restart command sequence.
- Logs showing reconnect.
- Screenshot before/after restart.

### Phase 7 — Chromium/CEF visual smoke

- [ ] Build required visual targets.
- [ ] Run QEMU virgl smoke with out-of-process GPU.
- [ ] Run `cefsimple --ozone-platform=qnx` without `--in-process-gpu`.
- [ ] Capture screenshot.
- [ ] Re-run representative headless tests to prove no regression.

Acceptance evidence:

- Build command/output summary.
- QEMU command.
- Screenshot path.
- Headless regression command/output summary.

## Current known facts

- QNX SDK root: `/home/yuta/qnx800`
- x86_64 and aarch64le sysroots both provide Screen/EGL/GLES2.
- `EGLNativeWindowType` on QNX maps to `screen_window_t`.
- QEMU virgl path works with:
  - `-vga none -device virtio-vga-gl -display gtk,gl=on`
  - runner option: `./tools/qnx_run.sh --virgl -- <command>`
- `egl-configs` and `gles2-gears` have already worked under QEMU virgl.
- Software Screen rendering via `screen_post_window()` has also worked under QEMU GUI.
- Phase 1A standalone probe showed:
  - EGL vendor/version: Mesa Project / EGL 1.5
  - `EGL_KHR_stream*`: absent in QEMU virgl
  - `EGL_QNX_image_native_buffer`: present
  - `EGL_EXT_image_dma_buf_import`, `EGL_EXT_image_dma_buf_import_modifiers`, `EGL_MESA_image_dma_buf_export`: present
  - `GL_OES_EGL_image`, `GL_OES_EGL_image_external`: present

## Change log

- 2026-07-02: Created plan after user approved out-of-process-first strategy with feasibility probes allowed.
- 2026-07-02: Phase 1A completed. EGL streams are absent in QEMU virgl; DMAbuf/EGLImage sharing is now the recommended candidate for Phase 1B, pending user decision.
- 2026-07-02: User approved the recommended DMAbuf probe path for Phase 1B. Plan updated with approved DMAbuf probe files and initial build/run commands before delegating work.
- 2026-07-02: Two broad Phase 1B worker attempts timed out. Read-only audit completed; plan updated to split Phase 1B into bounded smoke and true-DMAbuf microtasks, added required `-lsocket`, and marked partial files as not accepted DMAbuf evidence.
- 2026-07-02: Phase 1B smoke and SCM_RIGHTS prerequisite reports completed. Plan updated to add a device-fd SCM_RIGHTS microtask before returning to true DMAbuf export/import.
- 2026-07-02: Phase 1B device-fd probe completed successfully. Plan updated to isolate true DMAbuf export in an export-only microtask before full producer/consumer import/display.
- 2026-07-02: Phase 1B-exportonly worker timed out after 15 minutes before writing the required report. It left partial/unaccepted `tools/qnx_probes/qnx_dmabuf_export_only_probe.c`. A read-only audit/compile microtask completed next.
- 2026-07-02: Phase 1B-exportonly audit report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-audit-2026-07-02.md` found compile/link passes, but the preferred non-pbuffer `eglCreateDRMImageMESA` Path A has a malformed attribute list and the file can fall into a known-risk pbuffer fallback. Source hardening completed next.
- 2026-07-02: Phase 1B-exportonly hardening report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-hardening-2026-07-02.md` fixed Path A attributes, guarded the pbuffer fallback behind `--allow-pbuffer-risk`, and compiled cleanly with `-Wall -Wextra`.
- 2026-07-03: Phase 1B-exportonly runtime report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-exportonly-runtime-2026-07-02.md` proved true DMAbuf export under QEMU virgl. Plan updated to proceed to producer/consumer import/bind only, with Screen display deferred.
- 2026-07-03: Phase 1B-dmabuf-import worker timed out after 15 minutes before writing the required report. It may have left partial/unaccepted changes in `qnx_dmabuf_ipc.h`, `qnx_dmabuf_export_producer.c`, `qnx_dmabuf_import_consumer.c`, or `README.md`. A read-only audit/compile microtask completed next.
- 2026-07-03: Phase 1B-dmabuf-import audit report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-audit-2026-07-02.md` found producer/consumer compile and mostly match the intended true-DMAbuf path, but parent-to-consumer IPC framing is wrong: the producer sends header and SCM_RIGHTS fds in separate stream writes while the consumer expects header+fds in one `recvmsg()`.
- 2026-07-03: Phase 1B-dmabuf-import framing report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-framing-2026-07-02.md` fixed the parent-to-consumer framing and compiled producer/consumer successfully.
- 2026-07-03: Phase 1B-dmabuf-import runtime report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-dmabuf-import-runtime-2026-07-02.md` proved true DMAbuf export + SCM_RIGHTS fd passing + `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` import + `glEGLImageTargetTexture2DOES` bind in separate processes under QEMU virgl.
- 2026-07-03: Phase 1B-display-isolation report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-isolation-2026-07-02.md` proved direct Screen-window EGL surface creation and swap.
- 2026-07-03: Phase 1B-display report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-display-2026-07-02.md` proved imported-texture Screen display composition. Plan updated to add browser-like restart probe files for producer crash/restart validation.
- 2026-07-03: Phase 1B-crash report `docs/qnx/history/research/qnx-ozone-oop-gpu-feasibility-phase1b-crash-2026-07-02.md` proved producer crash/restart with persistent browser-owned Screen window. Phase 1 feasibility is complete; proceed to Phase 2 design note and user approval before Chromium/Ozone implementation.
- 2026-07-03: Phase 2 design note written at `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`. Plan updated with final class responsibility summary. User requested design revision instead of implementation approval.
- 2026-07-03: Revised design note to make production IPC Mojo-based: DMAbuf fds travel as Mojo `handle<platform>` values in QNX-local mojom messages; raw Unix socket + `SCM_RIGHTS` is probe-only evidence.
- 2026-07-03: User approved starting implementation. Phase 3 GN/Ozone wiring is now active and will be delegated as the first implementation step.
- 2026-07-03: Phase 3 initial worker added opt-in GN/Ozone wiring and stubs. Reviewer found a likely compile blocker from incomplete `std::unique_ptr<PlatformScreen>` / `std::unique_ptr<InputMethod>` return types and missing `//ui/base/ime` dependency.
- 2026-07-03: Phase 3 stub compile blocker fixed and validated. `gn gen out/qnx_phase3` with `ozone_platform_qnx=true` succeeded, and `ninja -C out/qnx_phase3 ui/ozone/platform/qnx:qnx` built the opt-in QNX Ozone stub target. Phase 3 is complete.
- 2026-07-03: Phase 4 activated. First substep is minimal Browser/UI-side QNX Screen skeleton (Screen context, window manager, window creation/destruction, compile validation) with GPU/Mojo frame transport still stubbed.
- 2026-07-03: Phase 4 Screen skeleton substep completed and validated (`ninja -C out/qnx_phase4 ui/ozone/platform/qnx:qnx` succeeded). Plan updated to delegate the event-source substep next.
- 2026-07-03: Phase 4 event-source substep completed and validated (`ninja -C out/qnx_phase4_event ui/ozone/platform/qnx:qnx` succeeded). Minimal Browser/UI Screen skeleton is complete; proceed to Phase 5 starting with QNX-local Mojo interface definitions.
- 2026-07-03: Phase 5 Mojo interface worker timed out after writing a report; review found blockers. Required fixes: use `gfx.mojom.AcceleratedWidget` instead of raw `uint32 widget`, consolidate duplicate mojom targets, and rerun corrected `gn gen`/ninja validation.
- 2026-07-03: Phase 5 Mojo interface blockers fixed and validated in `docs/qnx/history/research/qnx-ozone-phase5-mojo-fix-2026-07-03.md`. `gn gen out/qnx_phase5_mojo_fix`, `ninja -C out/qnx_phase5_mojo_fix ui/ozone/platform/qnx/mojom:mojom`, and `ninja -C out/qnx_phase5_mojo_fix ui/ozone/platform/qnx:qnx` succeeded.
- 2026-07-03: Phase 5 GPU producer/export scaffold completed in `docs/qnx/history/research/qnx-ozone-phase5-gpu-producer-scaffold-2026-07-03.md`. `QnxRenderProducer`, `QnxSurfaceFactoryOzone`, and `QnxGLOzoneEGL` compile in `out/qnx_phase5_gpu`. Temporary root-source validation files were cleaned. Proceed to runtime Mojo SubmitFrame / browser host binding substep.
- 2026-07-03: Phase 5 Mojo host scaffold worker timed out before writing the required report. It left partial/unaccepted QNX host files/edits under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`. Read-only audit completed in `docs/qnx/history/research/qnx-ozone-phase5-mojo-host-audit-2026-07-03.md`: `qnx_gpu_host.{cc,h}` appears coherent, but the aggregate target fails on `NOTREACHED_IN_MIGRATION` and EGL include/type ordering, and the current `AddInterfaces` binding likely has wrong process semantics.
- 2026-07-03: Bounded compile fixes completed in `docs/qnx/history/research/qnx-ozone-phase5-host-compile-fix-2026-07-03.md`; `ui/ozone/platform/qnx:qnx` builds in `out/qnx_phase5_host_compile_fix`. Browser host binding remains unaccepted pending a process-direction design investigation for a browser-owned `QnxGpuHost` receiver path.
- 2026-07-03: Binding-path design completed in `docs/qnx/history/research/qnx-ozone-phase5-mojo-binding-path-design-2026-07-03.md`. Direct `AddInterfaces` registration of browser-owned `QnxGpuHost` is rejected; recommended path is QNX `GpuPlatformSupportHost` plus GPU-side startup interface. User approved `QnxGpuService` as the startup interface name before implementation.
- 2026-07-03: Phase 5 Mojo service binding implemented in `docs/qnx/history/research/qnx-ozone-phase5-mojo-service-binding-2026-07-03.md`. Browser-owned `QnxGpuHost` is passed to GPU-side `QnxGpuService.Initialize(...)` through `GpuPlatformSupportHost::OnGpuServiceLaunched()`, and `ninja -C out/qnx_phase5_mojo_service ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx` succeeded. Next: QnxGpuControl attach/generation lifecycle and metadata-only SubmitFrame exercise.
- 2026-07-03: Attach/generation lifecycle worker timed out before writing the required report. Partial/unaccepted edits may exist under `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/`; timeout artifact indicates it was fixing `binder.Run()` message-pipe handling. Read-only audit completed in `docs/qnx/history/research/qnx-ozone-phase5-attach-generation-audit-2026-07-03.md`: QNX widget detach/snapshot helpers and GPU-side `QnxGpuControl` implementation exist, but compile is blocked by incorrect `PendingReceiver` pipe passing, and `SubmitFrame` lacks generation equality validation.
- 2026-07-03: Attach/generation bounded fixes completed in `docs/qnx/history/research/qnx-ozone-phase5-attach-generation-fix-2026-07-03.md`; `control_receiver.PassPipe()` is used correctly, `QnxGpuHost::SubmitFrame` checks frame generation against the widget record, and `ninja -C out/qnx_phase5_attach_generation_fix ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx` succeeded.
- 2026-07-03: GPU-side render-producer lifecycle completed in `docs/qnx/history/research/qnx-ozone-phase5-render-producer-lifecycle-2026-07-03.md`; `QnxGpuService` creates/resizes/destroys `QnxRenderProducer` instances, has `SubmitTestFrameForWidget(...)`, and `ninja -C out/qnx_phase5_render_lifecycle ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx` succeeded.
- 2026-07-03: Browser-side import/display scaffold completed in `docs/qnx/history/research/qnx-ozone-phase5-browser-import-display-scaffold-2026-07-03.md`; `QnxFrameImporter` imports single-plane linear DMAbuf frames with `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` and displays through direct `eglCreateWindowSurface(screen_window_t)`, and `ninja -C out/qnx_phase5_browser_import_display ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx` succeeded. Root temp GN patch state required manual cleanup after the worker report; root `build/config/ozone.gni` is clean again.
- 2026-07-03: Attach-time SubmitFrame trigger completed in `docs/qnx/history/research/qnx-ozone-phase5-submitframe-trigger-2026-07-03.md`; `QnxGpuService::AttachWidget`/`ResizeWidget` call `SubmitTestFrameForWidget(...)` when the browser host remote is bound and the producer is valid, and `ninja -C out/qnx_phase5_submitframe_trigger ui/ozone/platform/qnx/mojom:mojom ui/ozone/platform/qnx:qnx` succeeded. Root temp GN patch state again required manual cleanup after the worker report; root `build/config/ozone.gni` is clean again.
- 2026-07-03: First runtime smoke attempt recorded in `docs/qnx/history/research/qnx-ozone-phase5-runtime-smoke-attempt-2026-07-03.md`. `ui/ozone/demo:ozone_demo` built (`6667/6667`), `ozone_demo --help` exits 0 under QNX, and `--ozone-platform=headless` times out without segfault, but `--ozone-platform=qnx` under QEMU virgl exits 139 in libc++ `std::__pad_and_output`. Full `cef:cefsimple` build was started but stopped around `9731/78833` because it was too broad for this smoke turn.
- 2026-07-03: Startup crash fixed in `docs/qnx/history/research/qnx-ozone-phase5-runtime-startup-crash-fix-2026-07-03.md` and cataloged in `docs/qnx/history/build-errors/test/runtime-assumption/qnx-ozone-demo-keyboard-layout-engine.md`. Root cause: QNX Ozone did not install a `KeyboardLayoutEngine`; `ozone_demo` dereferenced it after `InitializeForUI()`. Runtime then reached the next blocker: GL display initialization fails, demo falls back to software rendering, and `SoftwareRenderer` fails because QNX `CreateCanvasForWidget()` is missing.
- 2026-07-03: Bounded demo render surface completed in `docs/qnx/history/research/qnx-ozone-phase5-demo-render-surface-2026-07-03.md`. `QnxSurfaceOzoneCanvas` provides `CreateCanvasForWidget()` and posts Skia raster pixels to QNX Screen. `ozone_demo --ozone-platform=qnx` under QEMU virgl no longer reports `Failed to create software surface`; screenshot capture succeeded at `out/qnx_phase5_runtime_fix/qnx-ozone-demo.bmp`.
- 2026-07-04: Current status / safety pause recorded in `docs/qnx/history/research/qnx-ozone-phase5-current-status-pause-2026-07-04.md`. Exploratory `content/shell:content_shell` build in `out/qnx_phase5_oop_smoke` did not produce `content_shell`; latest observed blocker was `content/public/test/mock_navigation_throttle_registry.h` signature drift (`AddThrottle` override has 1 parameter while base has 2). This is not accepted OOP validation. Temporary root Chromium patch state was cleaned.
- 2026-07-04: OOP smoke target audit completed in `docs/qnx/history/research/qnx-ozone-phase5-oop-smoke-target-audit-2026-07-04.md`. Key finding: `ozone_demo` uses `single_process=true` so it never calls `OnGpuServiceLaunched`; `content_shell` is the only viable OOP smoke target found. Commands corrected to use `./tools/...` paths and note that `out/qnx_release` must be regenerated via `cef_create_projects_qnx.sh` to include newly committed Phase 5 files.
- 2026-07-04: `--ozone-qnx-gpu-trace` diagnostic switch implemented in `qnx_gpu_service.cc` and `qnx_gpu_host.cc`. Report in `docs/qnx/history/research/qnx-ozone-phase5-gpu-trace-logging-2026-07-04.md`. Narrow compile validation at `-j10` succeeded: `ui/ozone/platform/qnx/mojom:mojom` + `ui/ozone/platform/qnx:qnx` built 10120/10120 steps with RC: 0. Report in `docs/qnx/history/research/qnx-ozone-phase5-gpu-trace-compile-2026-07-04.md`.
- 2026-07-04: User-approved `content/shell:content_shell` attempt at `-j10` with `--ozone-qnx-gpu-trace` was stopped. Bootstrap reported 10 patches failed (`base_posix_elf_reader_qnx`, `chrome_browser_linux_is_qnx`, `first_run_dialog_qnx`, and 7 others). The tree is dirty; the result is not accepted. Build then failed on the dirty tree with the wrong GN label `content_shell:content_shell`, restarted with correct label, and progressed to only ~[140/44295] before subagent timeout. First blockers visible in the log: `os_crypt_linux.cc` (unknown identifiers / atomic_ref), `update_query_params.cc` (`#error unknown os`), `policy_constants.cc` (zero-length array to span), `sandbox/linux/proc_util.cc` (`d_type`/`DT_LNK`), `sandbox/linux/syscall_wrappers.cc` and `scoped_process.cc` (`sys/syscall.h` missing). Crashpad/farmhash/libsync blockers reported by timed-out worker were not confirmed in the visible log. Next required step is QNX bootstrap recovery (clean tree, fix or revert the 10 failing patches) before another `content/shell:content_shell` attempt. Report in `docs/qnx/history/research/qnx-ozone-phase5-content-shell-trace-run-2026-07-04.md`.
