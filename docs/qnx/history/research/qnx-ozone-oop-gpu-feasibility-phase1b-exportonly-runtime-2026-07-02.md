# QNX Ozone OOP-GPU Phase 1B-exportonly bounded runtime microtask

Date: 2026-07-03
Scope: bounded runtime probe, default path only; no `--allow-pbuffer-risk`.

## Goal

Determine whether QEMU virgl can produce at least one real DMAbuf fd via
`eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` (Path A), without
entering the known Mesa/QNX virgl pbuffer crash path.

## Exact commands run

**Compile:**
```sh
cd /home/yuta/chromium/src/cef
source ../out/qnx_release/qnx_env.sh
export PATH="$QNX_HOST/usr/bin:$PATH"
qcc -Wall -Wextra -Vgcc_ntox86_64 \
    -o ../out/qnx_release/qnx_dmabuf_export_only_probe \
    tools/qnx_probes/qnx_dmabuf_export_only_probe.c \
    -lscreen -lEGL -lGLESv2
```

**QEMU runtime:**
```sh
cd /home/yuta/chromium/src/cef
timeout 180 ./tools/qnx_run.sh --virgl --kill-existing -- \
    ./qnx_dmabuf_export_only_probe
```

## Compile result

**PASSED** — exit 0, no stdout/stderr, no warnings, no errors.

Binary: `out/qnx_release/qnx_dmabuf_export_only_probe`

## QEMU runtime result

**PASSED** — exit 0, completed in under 5 seconds.

QEMU runner: virgl (`-vga none -device virtio-vga-gl`), GTK display.

## Extension/function-pointer gate result (Phase 4)

All gates PASSED:

| Extension | Status |
|----------|--------|
| `EGL_MESA_image_dma_buf_export` | [PRESENT] |
| `EGL_MESA_drm_image` | [PRESENT] |
| `EGL_EXT_image_dma_buf_import` | [PRESENT] |
| `EGL_EXT_image_dma_buf_import_modifiers` | [PRESENT] |
| `EGL_KHR_gl_texture_2D` | [PRESENT] |
| `EGL_KHR_surfaceless_context` | [PRESENT] |

| Function pointer | Status |
|-----------------|--------|
| `eglCreateImageKHR` | [RESOLVED] |
| `eglDestroyImageKHR` | [RESOLVED] |
| `eglExportDMABUFImageQueryMESA` | [RESOLVED] |
| `eglExportDMABUFImageMESA` | [RESOLVED] |
| `eglCreateDRMImageMESA` | [RESOLVED] |

## Path A result

```
[Path A] eglCreateDRMImageMESA: created 389b83d3c0
[Path A] Source: Mesa-internal DRM buffer (no GL, no Screen).
```

`eglCreateDRMImageMESA` returned a valid handle (not `EGL_NO_IMAGE_KHR`).

Path B was not attempted (disabled by default, `--allow-pbuffer-risk` not passed).

## True DMAbuf export result and fd metadata

**MILESTONE ACHIEVED: TRUE DMAbuf EXPORT SUCCEEDED**

```
[Export] Query OK: fourcc=0x34325241 (AR24), planes=1, modifier=0x0
[Export] eglExportDMABUFImageMESA: SUCCESS
[Export] Valid fds found: 1
```

### Plane 0 fd metadata

| Field | Value |
|-------|-------|
| fd | 6 |
| stride | 256 bytes |
| offset | 0 bytes |
| fstat | OK |
| st_dev | 0x0 |
| st_ino | 0 |
| st_mode | 0666 (not S_ISCHR, not S_ISBLK, not S_ISREG — likely DMAbuf/匿名fd) |
| st_size | 0 |
| fcntl flags | 0x7 (O_RDWR=1, O_WRONLY=0, O_NONBLOCK=0) |

The fd is open for read-write, `st_mode` 0666 with no file-type bits set is
consistent with an anonymous DMAbuf/PRIME fd exported from Mesa's DRM internals.
`st_dev=0` and `st_ino=0` are expected for anonymous device-backed fds.

## Explicit milestone statement

**Phase 1B-exportonly milestone: PASSED.**

`eglCreateDRMImageMESA` + `eglExportDMABUFImageMESA` produced at least one real
DMAbuf fd (fd=6, AR24, 256-byte stride, O_RDWR) with valid `fstat`/`fcntl`
metadata, under QEMU virgl, default path, without entering the pbuffer
fallback.

## Stop-condition compliance

| Stop condition | Triggered? |
|----------------|------------|
| Compile/link failure | No |
| Required extensions or function pointers absent at Phase 4 gate | No — all present/resolved |
| `eglCreateDRMImageMESA` returned `EGL_NO_IMAGE_KHR` | No — returned valid handle |
| First successful export of >=1 DMAbuf fd with metadata | Yes — achieved at Plane 0 |
| QEMU timeout/crash/hang | No — completed cleanly, exit 0 |

## Non-goals verified

- No IPC attempted (no `sendmsg`/`recvmsg`).
- No import attempted (no `eglCreateImageKHR` with `EGL_LINUX_DMA_BUF_EXT`).
- No display/screen composition attempted.
- No `--allow-pbuffer-risk` passed.

## Residual notes

1. `libEGL warning: qs_destroy_loader_image_state(): LoaderPrivate argument is not NULL, can't handle this!` — a non-fatal Mesa library warning at shutdown; does not affect export success.

2. `st_mode=0666` with no file-type bits set is the observed DMAbuf fd pattern under QEMU virgl/Mesa. This is consistent with anonymous PRIME/DMAbuf fds and is not an error.

3. This probe does not validate that the exported fd can be **imported** successfully by a consumer via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` in a separate process — that is the next microtask (Phase 1B-dmabuf).

## Next smallest microtask

**Phase 1B-dmabuf**: true DMAbuf export + SCM_RIGHTS pass + import.

Acceptance requires:
1. Producer exports >=1 DMAbuf fd via `eglExportDMABUFImageMESA` (done here).
2. Producer passes fd via `sendmsg(SCM_RIGHTS)` (requires `qnx_dmabuf_ipc.h` helpers).
3. Consumer receives fd via `recvmsg(SCM_RIGHTS)` (Phase 1B-scmrights device-fd evidence: character-device fd passes cleanly).
4. Consumer imports via `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT, ...)` and binds via `glEGLImageTargetTexture2DOES`.

Current partial `qnx_dmabuf_export_producer.c` / `qnx_dmabuf_import_consumer.c` must be updated to replace the raw-pixel/socket fallback with true DMAbuf fd passing before this milestone can be accepted.

## Plan update checklist

- [x] Phase 1B-exportonly bounded runtime microtask: **PASSED**
- [ ] Phase 1B-dmabuf: update producer/consumer to use true DMAbuf export/import
- [ ] Update `docs/qnx/ozone-out-of-process-gpu-plan.md` Phase 1B checklist
