# Phase 7 CEF runtime diagnostics

- Date: 2026-07-10
- Scope: `//cef:cefsimple` under QEMU virgl with OOP GPU
- Status: diagnostic only; no runtime-behavior fix applied

## Clean rebuild

The local `out/qnx_release` directory was removed because its `locales/`
directory was `root:root` mode `0700`, blocking Ninja before any CEF action
could start. The standard clean bootstrap was then run:

```bash
cd chromium/src
git checkout -f
gclient sync -f -R
./cef/tools/qnx_sync_sources.sh -f -R
./cef/tools/cef_create_projects_qnx.sh \
  --build-type Release --qnx-sdp-root /home/yuta/qnx800
./out/qnx_release/ninja_qnx.sh cefsimple -k 20
```

Results:

- Bootstrap: `485 patches total (465 applied, 20 skipped, 0 failed)`.
- `cefsimple`: `78,689/78,689` actions, exit 0.
- Generated `out/qnx_release/locales`: `yuta:yuta`, mode `0775`.

The ownership failure was therefore local output residue, not a CEF GN/source
failure.

## Diagnostic patch

`qnx_phase7_cef_runtime_diagnostics` adds no behavior change. It emits:

1. QNX platform-window creation/constructor markers.
2. `QnxRenderProducer::ExtensionReport()` only when the existing DMAbuf export
   capability gate fails.

The patch was generated from the new-files baseline plus Phase 5/6 patches
1–9. A fresh intermediate tree applies it with `git apply --check -p0`, and
all three patched QNX files are byte-identical to the incrementally-built
working tree.

## Runtime results

### Default CEF Views mode

```bash
./tools/qnx_run.sh --virgl --kill-existing --timeout 45 -- \
  './cefsimple --ozone-platform=qnx --use-gl=egl --no-sandbox \
  --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr'
```

Result: exit 139 after:

```text
[QNX-TRACE] OnGpuServiceLaunched ...
QNX_OZONE_GPU_TRACE QnxGpuService::Initialize: gpu_host_remote bound
```

No `OzonePlatformQnx::CreatePlatformWindow` diagnostic marker occurs. The
failure is therefore before QNX's platform-window entry point, in the CEF
Views browser-creation path. It is not the prior cross-interface Mojo ordering
race: the Initialize acknowledgement path is already reached.

### Native CEF mode

```bash
./tools/qnx_run.sh --virgl --kill-existing --timeout 60 -- \
  './cefsimple --ozone-platform=qnx --use-gl=egl --no-sandbox --use-native \
  --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr'
```

Native mode reaches QNX window construction and GPU attachment:

```text
[QNX-TRACE] OzonePlatformQnx::CreatePlatformWindow: bounds=10,10 1004x748
[QNX-TRACE] QnxWindow::QnxWindow: widget=1 ...
QNX_OZONE_GPU_TRACE QnxGpuService::AttachWidget: widget=1 generation=1
```

It then rejects the render producer:

```text
EGL_MESA_drm_image:           ABSENT
EGL_MESA_image_dma_buf_export:ABSENT
eglCreateDRMImageMESA:        NOT RESOLVED
eglExportDMABUFImageMESA:     NOT RESOLVED
```

This differs from the `content_shell` Phase 5 smoke, where the same virgl
configuration reached `accepted=true` and `eglSwapBuffers reached`. The CEF
GPU initialization path is consequently selecting or initializing a different
EGL display/driver state. The next fix must investigate CEF's GPU GL/EGL
initialization path; do not weaken the producer gate or emulate the missing
extension.

## Residual blockers

1. Default CEF Views crash before `CreatePlatformWindow`.
2. Native CEF GPU-process EGL capability mismatch, which prevents
   `SubmitFrame`.
3. Automated screenshot remains unresolved: host desktop capture is black and
   QEMU 10.1.2 HMP `screendump` returns `Error: no surface` for
   `virtio-vga-gl`.
