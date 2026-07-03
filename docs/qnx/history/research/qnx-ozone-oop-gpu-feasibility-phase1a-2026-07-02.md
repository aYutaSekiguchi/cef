# QNX Ozone OOP-GPU Feasibility — Phase 1A Research Report
**Date:** 2026-07-02
**Probe:** `qnx_egl_extension_probe` (tools/qnx_probes/)
**QEMU environment:** x86_64, virgl, virtio-vga-gl, Mesa 1.5 EGL driver
**Exit code:** 0 (clean)

---

## Files Changed

| File | Action |
|------|--------|
| `tools/qnx_probes/README.md` | created |
| `tools/qnx_probes/qnx_egl_extension_probe.c` | created |
| `out/qnx_release/qnx_egl_extension_probe` | built binary |

---

## Exact Commands Run

**Build:**
```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Vgcc_ntox86_64 -o ../out/qnx_release/qnx_egl_extension_probe \
  tools/qnx_probes/qnx_egl_extension_probe.c -lscreen -lEGL -lGLESv2
```

**Run:**
```sh
cd /home/yuta/chromium/src/cef
./tools/qnx_run.sh --virgl --kill-existing -- ./qnx_egl_extension_probe
```

---

## Summarized Output

### Phase 1 — Screen context
- `screen_create_context(SCREEN_APPLICATION_CONTEXT)` **succeeded** (type=0)
- Screen library is functional in QEMU virgl environment.

### Phase 2–3 — EGL identity
| Property | Value |
|----------|-------|
| EGL version | 1.5 |
| Vendor | Mesa Project |
| Client APIs | OpenGL_ES |

EGL initialized via `eglGetDisplay(EGL_DEFAULT_DISPLAY)`.

### Phase 4–5 — EGL extension inventory (display + client)

**Extension availability table:**

| Extension | Status | Notes |
|----------|--------|-------|
| `EGL_KHR_stream` | **ABSENT** | core EGL stream API |
| `EGL_KHR_stream_producer_eglsurface` | **ABSENT** | producer-side EGLSurface attach |
| `EGL_KHR_stream_cross_process_fd` | **ABSENT** | FD-based stream IPC |
| `EGL_KHR_stream_consumer_gltexture` | **ABSENT** | GL texture external consumer |
| `EGL_KHR_stream_fifo_sync` | **ABSENT** | FIFO sync |
| `EGL_QNX_platform_screen` | **ABSENT** | QNX-native Screen platform dispatch |
| `EGL_QNX_image_native_buffer` | **PRESENT** | QNX native buffer → EGLImage |
| `EGL_EXT_platform_base` | **PRESENT** | generic `eglGetPlatformDisplayEXT` |
| `EGL_EXT_platform_device` | ABSENT | |
| `EGL_EXT_client_extensions` | PRESENT | client-string marker |
| `EGL_KHR_create_context` | PRESENT | core context creation |
| `EGL_KHR_surfaceless_context` | PRESENT | no-surface GL context |
| `EGL_KHR_no_config_context` | PRESENT | configless context |
| `EGL_KHR_wait_sync` | PRESENT | sync primitives |
| `EGL_MESA_drm_image` | PRESENT | DRM buffer import |
| `EGL_MESA_image_dma_buf_export` | PRESENT | DMAbuf export |
| `EGL_EXT_image_dma_buf_import` | PRESENT | DMAbuf import |
| `EGL_EXT_image_dma_buf_import_modifiers` | PRESENT | modifier support |
| `EGL_WL_bind_wayland_display` | PRESENT | Wayland interop |

### Phase 6 — EGL function pointer resolution

| Function | Status |
|----------|--------|
| `eglGetPlatformDisplayEXT` | **RESOLVED** |
| `eglGetPlatformDisplayQNX` | NOT FOUND |
| `eglCreateStreamKHR` | NOT FOUND |
| `eglDestroyStreamKHR` | NOT FOUND |
| `eglQueryStreamKHR` | NOT FOUND |
| `eglCreateStreamProducerSurfaceKHR` | NOT FOUND |
| `eglGetStreamFileDescriptorKHR` | NOT FOUND |
| `eglCreateStreamFromFileDescriptorKHR` | NOT FOUND |
| `eglStreamConsumerGLTextureExternalKHR` | NOT FOUND |
| `eglStreamConsumerAcquireKHR` | NOT FOUND |
| `eglStreamConsumerReleaseKHR` | NOT FOUND |

`eglGetPlatformDisplayEXT` works, confirming the EXT platform base dispatch path.
All KHR_stream functions are unavailable — the extension string is absent.

### Phase 7–8 — GLES extension inventory

GLES2 context created via pbuffer; `glGetString(GL_EXTENSIONS)` succeeded (3812 chars).

| Extension | Status |
|----------|--------|
| `GL_OES_EGL_image` | **PRESENT** |
| `GL_OES_EGL_image_external` | **PRESENT** |
| `GL_EXT_texture_sRGB_decode` | PRESENT |
| `GL_OES_rgb8_rgba8` | PRESENT |

`GL_OES_EGL_image` and `GL_OES_EGL_image_external` are both available, which satisfies the GL-side requirement for EGLImage-backed texture sharing.

---

## Phase Gate Decision

**EGL_KHR_stream is ABSENT in the QEMU virgl Mesa driver.**

This means the Phase 1B stream producer/consumer pipeline **is not viable in the current QEMU environment**. The Mesa virgl driver does not expose the KHR stream family.

However, the probe reveals two positive paths:

1. **DMAbuf** (most promising for virgl): `EGL_EXT_image_dma_buf_import`, `EGL_EXT_image_dma_buf_import_modifiers`, and `EGL_MESA_image_dma_buf_export` are all present. virtio-gpu natively supports DMAbuf buffers via the `VIRTIO_GPU_CMD_RESOURCE_CREATE_BLOB` path and the DMAbuf fd sharing protocol. Chromium's Ozone/GBM backend already uses DMAbuf; the QNX equivalent would wire DMAbuf FDs to Screen buffers via `EGL_QNX_image_native_buffer` or via the DMAbuf import path.

2. **EGLImage + GL_OES_EGL_image**: The GL-side pieces for EGLImage-based sharing are fully available. Combined with `EGL_QNX_image_native_buffer`, this could support a custom QNX-native buffer-sharing path, but would require more engineering.

3. **`EGL_EXT_platform_base`**: `eglGetPlatformDisplayEXT` is resolved, which could be used to implement a QNX-native platform dispatch if the QNX-specific display extension were present. However, `EGL_QNX_platform_screen` is absent, so the QNX screen platform is not accessible through EGL dispatch.

---

## Implications for Phase 1B Planning

| Question | Answer |
|----------|--------|
| Can EGL_KHR_stream work? | **No** — absent from virgl Mesa. Would need a real GPU/QNX hardware with QNX EGL driver that supports streams. |
| What sharing mechanism to use instead? | **DMAbuf** is the strongest candidate. Already present in virgl, compatible with Chromium's existing GBM/DMAbuf Ozone path, and supports cross-process FD sharing. |
| Is `EGL_QNX_image_native_buffer` useful without streams? | Yes — it enables Screen-native buffers to be imported as EGLImages. Combined with `GL_OES_EGL_image`, this could support a direct GPU-process rendering path without streams. |
| Should Phase 1B use real hardware or try DMAbuf in QEMU first? | **Recommend trying DMAbuf probe in QEMU first.** If DMAbuf works with virtio-gpu in QEMU, Phase 1B could be built and validated on QEMU before moving to real hardware. |

---

## Blockers / Requested Plan Updates

1. **Phase 1B (stream producer/consumer) is blocked in QEMU virgl** because `EGL_KHR_stream` is absent from the Mesa virgl driver. Two options:
   - **Option A**: Re-scope Phase 1B to target DMAbuf instead of EGL streams (avoids hardware dependency, works in QEMU).
   - **Option B**: Keep Phase 1B targeting EGL streams but document that it requires real QNX hardware for validation.

2. **`EGL_QNX_platform_screen` is absent** — the QNX-native EGL platform dispatch is not available in the virgl driver. This is expected; real QNX hardware with QNX GPU drivers would need to be tested to confirm this extension.

3. **No other blockers** — the probe itself works correctly, Screen context creation succeeds, EGL 1.5 is functional, and GLES2 extensions including `GL_OES_EGL_image` are available.

---

## Recommended Next Step

Before proceeding to Phase 1B, update the plan to choose between:
- **Option A** (preferred): Replace the EGL stream producer/consumer probe with a **DMAbuf + QNX native buffer probe** — this is the path most likely to work in QEMU virgl and map to Chromium's existing DMAbuf Ozone infrastructure.
- **Option B**: Add a **real-QNX-hardware-only** Phase 1B clause for EGL stream validation, keeping the QEMU path on a fallback DMAbuf probe.

If Option A is chosen, the Phase 1B probe files (`qnx_screen_stream_consumer.c`, `qnx_egl_stream_producer.c`) should be retargeted to:
1. Create a DMAbuf fd in the producer process using `EGL_MESA_image_dma_buf_export`
2. Share the fd over a pipe or socket
3. Import it in the consumer process using `EGL_EXT_image_dma_buf_import`
4. Attach the resulting EGLImage to a GL texture and composite via Screen
