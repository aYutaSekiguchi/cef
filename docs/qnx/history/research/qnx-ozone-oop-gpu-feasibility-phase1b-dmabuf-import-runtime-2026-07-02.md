# QNX Ozone OOP-GPU Phase 1B-dmabuf-import runtime microtask

Date: 2026-07-03
Scope: bounded QEMU virgl run of true DMAbuf producer/consumer import + bind; no Screen display.

## Exact commands run

### Compile (producer)

```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_export_producer \
  tools/qnx_probes/qnx_dmabuf_export_producer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```

Result: **pass** — exit 0, no warnings, no errors.

### Compile (consumer)

```sh
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
  -o ../out/qnx_release/qnx_dmabuf_import_consumer \
  tools/qnx_probes/qnx_dmabuf_import_consumer.c \
  -lsocket -lscreen -lEGL -lGLESv2
```

Result: **pass** — exit 0, one pre-existing warning (unchanged from prior microtasks):

```
tools/qnx_probes/qnx_dmabuf_import_consumer.c:326:12:
  warning: 'composite_to_screen' defined but not used [-Wunused-function]
```

### QEMU runtime run

```sh
cd /home/yuta/chromium/src/cef
timeout 240 ./tools/qnx_run.sh --virgl --kill-existing -- \
  'rm -f /tmp/qnx_dmabuf_probe.sock /tmp/qnx-dmabuf-consumer.bmp; \
   ./qnx_dmabuf_import_consumer & sleep 1; \
   ./qnx_dmabuf_export_producer; wait'
```

Result: **pass** — runner exit 0, QEMU launched and exited cleanly within timeout.

---

## QEMU result

- QEMU launched successfully with virgl (`-vga none -device virtio-vga-gl -display gtk,gl=on`).
- Guest shell reached, NFS mounted, binaries executed.
- No QEMU crash, no guest SIGSEGV, no 180 s timeout.
- Runner log: `/home/yuta/chromium/src/out/qnx_release/qnx_run_20260703_002729_qnx_dmabuf_export_producer__wait.log`

---

## Producer export metadata

```
[GPU CHILD] Starting Path A DMAbuf export...
[GPU CHILD] EGL 1.5 initialized (display=5884ea2180)
[GPU CHILD]   vendor: Mesa Project
[GPU CHILD]   EGL_MESA_drm_image:           [PRESENT]
[GPU CHILD]   EGL_MESA_image_dma_buf_export: [PRESENT]
[GPU CHILD]   eglCreateDRMImageMESA:        [RESOLVED]
[GPU CHILD]   eglExportDMABUFImageMESA:    [RESOLVED]

[GPU CHILD] === Path A: EGL_MESA_drm_image ===
[GPU CHILD]   eglCreateDRMImageMESA: created 5884eaa3c0
[GPU CHILD]   Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0
[GPU CHILD]   eglExportDMABUFImageMESA: SUCCESS
[GPU CHILD]   Plane 0: fd=7 stride=256 offset=0
[GPU CHILD]   Total valid plane fds: 1
[GPU CHILD]   IPC header: 64x64 fourcc=0x34325241 n_planes=1 exported=1
[GPU CHILD]   stride=256 offset0=0 modifier0=0x00000000
```

- Path A used: `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` (no GL, no Screen, no pbuffer).
- DMAbuf format: fourcc `0x34325241` ("AR24"), 1 plane, modifier 0.
- Plane 0 fd: 7, stride 256, offset 0.
- `hdr.exported = 1` (DMAbuf export succeeded, not raw-pixel fallback).
- This confirms the same export path proven in Phase 1B-exportonly, now working within the multi-process producer/consumer context.

---

## SCM_RIGHTS fd passing result

```
[GPU CHILD]   Sending header to parent via socketpair (fd=4)...
[GPU CHILD]   Sent 56-byte header to parent.
[PARENT] Received IPC header: 64x64 fourcc=0x34325241 n_planes=1 exported=1
[GPU CHILD]   Sending 1 plane fds via sendmsg(SCM_RIGHTS)...
[GPU CHILD]     SCM_RIGHTS fd[0] = 7
[GPU CHILD]   sendmsg(SCM_RIGHTS) returned 10.
[PARENT]   Received plane FD[0] from GPU child: 4
[GPU CHILD]   Waiting for parent relay acknowledgment...
[PARENT] Total plane FDs received from GPU child: 1
[PARENT] Forwarding header + 1 plane fds to consumer via single sendmsg(SCM_RIGHTS)...
[PARENT]   Forwarding plane FD[0] = 4
[PARENT] sendmsg(header+SCM_RIGHTS) to consumer returned 56 (expected 56).
[PARENT] Waiting for consumer ACK...
[CONSUMER]   Received plane FD[0]: 7 (cmsg_level=65535)
  IPC header: 64x64 stride=256 fourcc=0x34325241 planes=1 exported=1
  SCM_RIGHTS FDs received: 1 (expected 1)
  IPC MILESTONE: SCM_RIGHTS fd passing succeeded.
```

- **Producer → parent (socketpair):** `send(hdr)` + `sendmsg(SCM_RIGHTS)` → parent received header + 1 fd.
- **Parent → consumer (Unix socket):** single `sendmsg()` with header as iovec + SCM_RIGHTS fds → consumer received header + 1 fd.
- `cmsg_level=65535` (0xFFFF) anomaly confirmed on consumer receive side (same as Phase 1B-devicefd/scmrights probes); does not prevent fd passing.
- No `EBADF`, no zero-fd count, no `recvmsg()` failure.
- **IPC MILESTONE: SCM_RIGHTS fd passing succeeded across all three legs (GPU child → parent → consumer).**

---

## Consumer EGL import result

```
[4/5] Creating GL texture...
  Attempting DMAbuf EGLImage import (n_fds=1)...
  Import attrs (14 entries): 64x64 fourcc=0x34325241 n_planes=1
  Import attrs: stride=256 offset0=0 modifier0=0x00000000
  EGLImage imported from DMAbuf: 30a61213b0
  MILESTONE: DMAbuf EGLImage IMPORT succeeded.
```

- `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` called with:
  - Display: consumer's EGL display (not the producer's)
  - Target: `EGL_LINUX_DMA_BUF_EXT` (0x3270)
  - Buffer: `NULL` (EGL no-native-buffer; fd is the actual resource)
  - Planes: 1 plane (fd=7, stride=256, offset=0)
  - Fourcc: 0x34325241
  - No modifier passed (EGL_EXT_image_dma_buf_import only, no modifier extension).
- **EGLImage created successfully: `30a61213b0`** (not `EGL_NO_IMAGE`).
- No EGL error was emitted.

---

## GL texture bind result

```
  GL texture created from EGLImage: 1
  MILESTONE: GL_OES_EGL_image texture binding succeeded.
  Texture created via DMAbuf EGLImage import.
```

- `glGenTextures(1, &tex)` → tex=1.
- `glBindTexture(GL_TEXTURE_2D, tex)` → current texture bound.
- `glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, egl_img)` called.
- `glGetError()` returned `GL_NO_ERROR` (0).
- `glTexParameteri` set to `GL_LINEAR`/`GL_CLAMP_TO_EDGE` (valid for bound EGLImage texture).
- **GL texture successfully created and bound from DMAbuf EGLImage.**
- **MILESTONE: GL_OES_EGL_image texture binding succeeded.**

---

## Screen composition

```
[5/5] Screen composition: SKIPPED (known SCREEN_PROPERTY_EGL_HANDLE blocker)
  Per Phase 1B-dmabuf microtask: do not attempt Screen display.
  Import/bind milestone only. Screen composition is Phase 1B-display.
```

- Screen window/display attempt was not entered. Per task constraints, `SCREEN_PROPERTY_EGL_HANDLE` is a known separate blocker from Phase 1B-smoke, not in scope for this microtask.

---

## True DMAbuf import/bind milestone: PASSED

All three legs of the DMAbuf pipeline are validated end-to-end under QEMU virgl:

| Milestone | Status |
|---|---|
| Producer: `eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` → real fd | ✅ PASS |
| IPC: SCM_RIGHTS fd passes from GPU child → parent → consumer | ✅ PASS |
| Consumer: `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` creates EGLImage | ✅ PASS |
| Consumer: `glEGLImageTargetTexture2DOES` binds EGLImage to GL texture | ✅ PASS |
| GL error check: `glGetError()` returns `GL_NO_ERROR` after bind | ✅ PASS |
| No pbuffer involved; no Mesa/QNX virgl crash | ✅ PASS |

The complete chain **GPU process → IPC fd passing → browser/consumer process → EGLImage → GL texture** is proven functional in QEMU virgl.

---

## Non-blocking notes

- The DRM fourcc reported by `eglExportDMABUFImageQueryMESA` is `0x34325241` ("AR24"), which is a 16-bit pixel format variant. The probe correctly reports what Mesa returns; the import accepted it without complaint. This is a format quirk, not a blocker.
- One pre-existing unused-function warning in the consumer source (`composite_to_screen`); unchanged from prior microtasks.
- `libEGL warning: qs_destroy_loader_image_state(): LoaderPrivate argument is not NULL, can't handle this!` — a loader-state cleanup warning emitted at exit; cosmetic, does not affect correctness.
- The consumer binary defaults to Screen composition enabled (no `--no-screen-composition` flag was passed), but the main control flow skipped `composite_to_screen()` because the loop structure goes straight to cleanup after `[5/5]`. The `--no-screen-composition` flag is present in the consumer source but was not needed because the Screen path was not entered.

---

## Next smallest microtask or blocker/requested plan update

The Phase 1B-dmabuf import/bind milestone is complete. The next smallest microtask is **Phase 1B-display**: attempt Screen composition of the imported texture via `SCREEN_PROPERTY_EGL_HANDLE` → `eglCreateWindowSurface` → `eglSwapBuffers`. This is the known separate blocker from Phase 1B-smoke that was deferred.

The `composite_to_screen()` function is already present in the consumer source (`tools/qnx_probes/qnx_dmabuf_import_consumer.c:326`). The next bounded microtask should:

1. Pass `--no-screen-composition` only if `SCREEN_PROPERTY_EGL_HANDLE` is absent; otherwise attempt composition.
2. Or split into two sub-microtasks: first validate `SCREEN_PROPERTY_EGL_HANDLE` availability in isolation, then wire it into the consumer.
3. Alternatively, document the `SCREEN_PROPERTY_EGL_HANDLE` failure as a QEMU virgl limitation and plan for real-QNX-hardware validation of Screen composition.

The orchestrator should update `docs/qnx/ozone-out-of-process-gpu-plan.md` Phase 1B checklist before the next worker is delegated.
