# QNX OOP-GPU Feasibility Probes

Phase 1 standalone probes for the QNX Ozone out-of-process GPU investigation.
These are standalone C programs that run inside QEMU virgl; they are NOT part of
the Chromium/Ozone build system.

## Files

| File | Purpose |
|------|---------|
| `qnx_egl_extension_probe.c` | Probe EGL vendor/version/client APIs and QNX/stream extension availability. |
| `qnx_dmabuf_ipc.h` | Shared IPC protocol definitions for the DMAbuf probe (magic, header, CMSG constants). |
| `qnx_dmabuf_export_producer.c` | **(Phase 1B-dmabuf)** DMAbuf producer: Path A (`eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA`), fork architecture (GPU child + socket parent), sends plane FDs via `sendmsg(SCM_RIGHTS)` to consumer. |
| `qnx_dmabuf_import_consumer.c` | **(Phase 1B-display)** DMAbuf consumer: receives plane FDs via `recvmsg(SCM_RIGHTS)`, imports via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)`, binds via `glEGLImageTargetTexture2DOES`, then composites the imported texture into a visible Screen/EGL window surface via direct `screen_window_t` → `eglCreateWindowSurface`. `SCREEN_PROPERTY_EGL_HANDLE` is non-fatal (QEMU virgl limitation); `--no-screen-composition` flag available for backward compatibility. |
| `qnx_scmrights_probe.c` | Phase 1B-scmrights prerequisite: validate QNX `SCM_RIGHTS` fd passing independently of EGL/DMAbuf. Character-device fd passing proven viable; regular-file fd passing has a QNX/QEMU kernel bug. |
| `qnx_dmabuf_export_only_probe.c` | Phase 1B-exportonly: isolate true DMAbuf export via Path A (EGL_MESA_drm_image, no GL) without IPC/import/display. Runtime proven: AR24, one plane, stride=256, modifier=0. Path B (pbuffer/EGL_GL_TEXTURE_2D_KHR fallback) is **disabled by default**. |
| `qnx_dmabuf_restart_consumer.c` | **(Phase 1B-crash)** Browser-like consumer: owns one persistent Screen window + EGL window surface for entire run, listens on `/tmp/qnx_dmabuf_restart.sock`, accepts two producer connections sequentially, imports each DMAbuf frame via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` + `glEGLImageTargetTexture2DOES`, renders to the same Screen window, swaps. Exits 0 only after both frames display successfully. |
| `qnx_dmabuf_restart_producer.c` | **(Phase 1B-crash)** Standalone producer: connects to consumer socket, exports one DMAbuf frame via Path A (`eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA`), sends header + SCM_RIGHTS FDs via `sendmsg(SCM_RIGHTS)`, waits for ACK, exits with configurable code (`--exit-code=N`). Designed to run twice: first with non-zero exit to simulate GPU crash, second with zero exit to simulate restart. |

## Phase 1B-crash build/run

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"

# Build
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_restart_consumer \
    tools/qnx_probes/qnx_dmabuf_restart_consumer.c \
    -lsocket -lscreen -lEGL -lGLESv2

qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_restart_producer \
    tools/qnx_probes/qnx_dmabuf_restart_producer.c \
    -lsocket -lscreen -lEGL -lGLESv2

# Run: consumer owns window; producer 1 exits 37 (crash sim), producer 2 exits 0
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_restart.sock; \
   ./qnx_dmabuf_restart_consumer & sleep 2; \
   ./qnx_dmabuf_restart_producer --exit-code=37; \
   ./qnx_dmabuf_restart_producer --exit-code=0; wait'
```

**Protocol**: Each producer connects to `/tmp/qnx_dmabuf_restart.sock`, sends one
DMAbuf frame (header + SCM_RIGHTS FDs via single `sendmsg()`), waits for ACK,
and exits with its configured code. The consumer accepts exactly two connections in
sequence and composites each frame to the same `screen_window_t`/`EGLSurface`.

**Consumer `--no-screen-composition` flag** (optional): skips Screen composition.
Screen composition is now **enabled by default**. `SCREEN_PROPERTY_EGL_HANDLE`
fails in QEMU virgl but is **non-fatal**: Mesa accepts raw `screen_window_t`
as `EGLNativeWindowType` directly. `composite_to_screen()` uses
`eglCreateWindowSurface(display, cfg, (EGLNativeWindowType)(uintptr_t)win, NULL)`
without querying `SCREEN_PROPERTY_EGL_HANDLE`. This was validated in
Phase 1B-display-isolation and Phase 1B-display (2026-07-03).

## Phase 1B-dmabuf build/run (reference; already validated)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"

# Build
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_export_producer \
    tools/qnx_probes/qnx_dmabuf_export_producer.c \
    -lsocket -lscreen -lEGL -lGLESv2

qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_import_consumer \
    tools/qnx_probes/qnx_dmabuf_import_consumer.c \
    -lsocket -lscreen -lEGL -lGLESv2

# Run (producer listens; consumer connects with retries)
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock; \
   ./qnx_dmabuf_import_consumer --no-screen-composition & sleep 1; \
   ./qnx_dmabuf_export_producer; wait'
```

## Phase 1B-exportonly build/run (reference; already validated 2026-07-03)

```sh
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_export_only_probe \
    tools/qnx_probes/qnx_dmabuf_export_only_probe.c \
    -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_dmabuf_export_only_probe
```

## Phase 1A build/run (reference)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_egl_extension_probe \
  tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2
./tools/qnx_run.sh --virgl -- ./qnx_egl_extension_probe
```

## Phase 1B SCM_RIGHTS build/run (reference; already validated)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_scmrights_probe \
  tools/qnx_probes/qnx_scmrights_probe.c -lsocket
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_scmrights_probe
```

## IPC protocol (qnx_dmabuf_ipc.h)

```
Producer fork architecture:
  - Parent: Unix-domain socket server (binds /tmp/qnx_dmabuf_probe.sock)
  - GPU child: creates Mesa DRM image (Path A: eglCreateDRMImageMESA),
               exports DMAbuf (eglExportDMABUFImageMESA),
               sends header + SCM_RIGHTS FDs to parent via socketpair
  - Parent: accepts consumer connection, receives FDs from GPU child,
            forwards header + SCM_RIGHTS FDs to consumer via sendmsg(SCM_RIGHTS)

Consumer:
  - Connects to producer's Unix socket (with retry loop)
  - Receives header + SCM_RIGHTS FDs via recvmsg(SCM_RIGHTS)
  - Imports via eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)
  - Binds via glEGLImageTargetTexture2DOES
  - Sends "CONSUMER_OK" ACK

IPC header fields:
  - magic, version: protocol identification
  - width, height: image dimensions
  - stride: bytes per scanline (plane 0)
  - fourcc: DRM fourcc (e.g. 0x34324241 = AR24)
  - n_planes: number of plane FDs (1–4)
  - exported: 0=failed, 1=DMAbuf (FDs via SCM_RIGHTS), 2=raw pixels (fallback)
  - offset0: byte offset for plane 0
  - modifier0_lo: modifier lower 32 bits (plane 0)
```

## Expected output (Phase 1B-dmabuf + display)

**Producer**:
- `[GPU CHILD] Path A: EGL_MESA_drm_image`
- `[GPU CHILD] eglCreateDRMImageMESA: created <ptr>`
- `[GPU CHILD] Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0`
- `[GPU CHILD] eglExportDMABUFImageMESA: SUCCESS`
- `[GPU CHILD] Plane 0: fd=N stride=256 offset=0`
- `[GPU CHILD] Sending <N> plane fds via sendmsg(SCM_RIGHTS)`

**Consumer**:
- `[CONSUMER] Connected to producer (sock_fd=<N>)`
- `[CONSUMER] SCM_RIGHTS FDs received: 1`
- `[CONSUMER] Import attrs: stride=256 offset0=0`
- `[CONSUMER] EGLImage imported from DMAbuf: <ptr>`
- `[CONSUMER] MILESTONE: DMAbuf EGLImage IMPORT succeeded.`
- `[CONSUMER] GL texture created from EGLImage: <N>`
- `[CONSUMER] MILESTONE: GL_OES_EGL_image texture binding succeeded.`
- `Screen window: size=64x64 pos=64,64 usage=SCREEN_USAGE_OPENGL_ES2 visible=1`
- `SCREEN_PROPERTY_EGL_HANDLE: not available (diagnostic only; non-fatal)`
- `eglCreateWindowSurface: SUCCESS surface=<ptr>`
- `MILESTONE: EGL window surface created from screen_window_t.`
- `eglMakeCurrent: OK`
- `Shader: prog=<N> u_tex=<loc> a_pos=<loc> a_tex=<loc>`
- `glBindTexture(tex=<N>): GL error=0x0`
- `glDrawArrays: GL error=0x0`
- `MILESTONE: DMAbuf-imported texture rendered to Screen window.`
- `eglSwapBuffers: OK`
- `MILESTONE: Screen display composition (eglSwapBuffers) succeeded.`

## Phase gate

Phase 1B (DMAbuf probe) depends on:
- `EGL_MESA_image_dma_buf_export` (present in virgl, Phase 1A)
- `EGL_EXT_image_dma_buf_import` (present in virgl, Phase 1A)
- `GL_OES_EGL_image` (present in virgl, Phase 1A)
- `EGL_MESA_drm_image` (present in virgl, Phase 1A-exportonly runtime)
- SCM_RIGHTS fd passing: character-device fds work; regular-file fds have QNX/QEMU kernel bug

Milestones for Phase 1B-dmabuf-import:
1. **[DONE]** `EGL_MESA_drm_image` present (Phase 1A)
2. **[DONE]** `eglExportDMABUFImageMESA` produces real DMAbuf fd (Phase 1B-exportonly)
3. **[DONE]** `sendmsg(SCM_RIGHTS)` sends >= 1 DMAbuf fd (Phase 1B-dmabuf producer)
4. **[DONE]** `recvmsg(SCM_RIGHTS)` receives >= 1 DMAbuf fd (Phase 1B-dmabuf consumer)
5. **[DONE]** `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` imports successfully (Phase 1B-dmabuf consumer)
6. **[DONE]** `glEGLImageTargetTexture2DOES` binds EGLImage to GL texture (Phase 1B-dmabuf consumer)

Milestones for Phase 1B-display:
7. **[DONE]** Screen window with `SCREEN_USAGE_OPENGL_ES2` created and made visible
8. **[DONE]** `eglCreateWindowSurface(display, cfg, screen_win, NULL)` succeeds without `SCREEN_PROPERTY_EGL_HANDLE`
9. **[DONE]** GLES2 shader program links; imported texture bound with GL error = 0
10. **[DONE]** `glDrawArrays` renders the imported texture to the window surface
11. **[DONE]** `eglSwapBuffers` posts the rendered frame to the visible window

Milestones for Phase 1B-crash (producer crash/restart):
12. **[DONE]** Consumer creates one persistent Screen context + window + EGL window surface at startup
13. **[DONE]** Consumer accepts two sequential producer connections on `/tmp/qnx_dmabuf_restart.sock`
14. **[DONE]** Producer 1 exits non-zero (--exit-code=37) after consumer imports/displays its frame; consumer window stays alive
15. **[DONE]** Producer 2 reconnects, sends a second DMAbuf frame; consumer imports/displays it on the **same** `screen_window_t`/`EGLSurface`
16. **[DONE]** Consumer exits 0 only after both frames display successfully
17. **[DONE]** Window identity confirmed stable across both frames (`screen_win` and `surf` pointers identical)

QNX hardware validation (not QEMU): not yet attempted
