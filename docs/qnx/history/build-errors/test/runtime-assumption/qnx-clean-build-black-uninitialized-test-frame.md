# Clean QNX CEF build displays an uninitialized black test frame

- Date: 2026-07-15
- Signature: `SubmitTestFrameForWidget` runs once; `OnCompositorPreSwap` and `CaptureCurrentFramebufferToDmaBuf` never run
- Stage: test
- Category: runtime-assumption
- Scope: QNX Ozone GPU producer / browser frame importer / QEMU virgl

## Symptoms

A clean CEF bootstrap and build loads `qnx-input-probe.html` successfully (`OnLoadEnd status=200`) but the QNX Screen window is black. Enabling `qnx_render_producer_solid_color` changes the same rectangle to sky blue rather than displaying page content.

The clean runtime log contains one attach-time frame:

```text
QnxGpuService::AttachWidget ... TRIGGER SubmitTestFrameForWidget
QnxGpuService::SubmitTestFrameForWidget ... planes=1
```

It contains no `OnCompositorPreSwap`, `CaptureCurrentFramebufferToDmaBuf`, or `source=compositor` records.

## Root cause

The attach-time `SubmitTestFrameForWidget` path exports a newly allocated Mesa DRM image. Without the Phase 5 solid-color scaffold, no compositor pixels are written into that image, so its contents are black. With the scaffold enabled, `PaintSolidColorToDmaBuf()` deliberately overwrites it with sky blue RGBA `(0, 0.498, 1.0, 1.0)`.

Actual page content requires the compositor pre-swap capture pipeline from `qnx_screen_bridge_render_only_fallback`. That patch had been disabled because its old hunks were generated against a dirty Chromium tree and failed on clean `qnx_frame_importer.cc:131`. Disabling it removed:

```text
SkiaOutputSurfaceImplOnGpu pre-swap hook
  -> QnxGpuService::OnCompositorPreSwap
  -> QnxRenderProducer::CaptureCurrentFramebufferToDmaBuf
  -> QnxGpuHost::SubmitFrame
  -> QnxFrameImporter::ImportAndDisplayFrame
```

Thus solid-color enabled meant sky blue; solid-color disabled meant black. Neither state contained real compositor content.

A separate validation trap was that `/export/chromium-src` was bind-mounted to `/home/yuta/chromium/src`. Running `chromium/test/src/cef/tools/qnx_run.sh` without changing the export still executed the main checkout binary under `/mnt/nfs`, not the clean test binary. Clean artifacts were therefore hard-linked into `/home/yuta/chromium/src/out/qnx_test_release` for reliable QEMU validation.

## Fix pattern

Regenerate `qnx_screen_bridge_render_only_fallback.patch` against the exact clean pre-screen state:

1. Install current `new_files` (including the folded `defer_gl` fix).
2. Apply all preceding enabled QNX patches.
3. Diff that state against the validated compositor-capture implementation using:
   `git diff --no-prefix --relative --full-index`.
4. Verify forward apply on the clean base and reverse apply on the result.
5. Re-enable the regenerated patch in `patch.cfg`.
6. Keep the standalone Phase 5 solid-color patch disabled.

Add a one-shot pixel marker immediately after the first successful `glReadPixels` to distinguish a valid content capture from an all-zero buffer.

## Applied change

- Re-generated `qnx_screen_bridge_render_only_fallback.patch` for six files:
  - `qnx_frame_importer.{cc,h}`
  - `qnx_gpu_service.{cc,h}`
  - `qnx_render_producer.{cc,h}`
- Re-enabled the patch in `patch.cfg`.
- Added one-shot `pixel_stats` output with byte count, non-zero byte count, sum, min, and max.
- Left `qnx_render_producer_solid_color` disabled.

## Verification

Clean-tree bootstrap:

```text
checkout_rc=0 gclient_rc=0 bootstrap_rc=0
492 patches total (471 applied, 21 skipped, 0 failed)
Success! QNX CEF project files created.
```

Clean `cefsimple` build:

```text
build_rc=0
[57293/57294] SOLINK ./libcef.so
```

QEMU virgl runtime (60 seconds):

```text
pixel_stats bytes=3003968 nonzero_bytes=3003968
            byte_sum=759014510 min=9 max=255
OnCompositorPreSwap: entered                    154 times
CaptureCurrentFramebufferToDmaBuf: captured    148 times
CreateMesaExportFrame: source=compositor       148 times
accepted=true display_ok=true                  148 times
PaintSolidColor                                0 times
segmentation violation                         0 times
OnLoadEnd status=200                           1 time
```

All captured bytes were non-zero and ranged from 9 to 255, proving the exported DMAbuf contained compositor pixels rather than the black uninitialized test frame.

## Files touched

- `patch/patch.cfg`
- `patch/patches/qnx/chromium/qnx_screen_bridge_render_only_fallback.patch`
- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/qnx-clean-build-black-uninitialized-test-frame.md`

## Related notes

- `docs/qnx/ozone-screen-dmabuf-oop-gpu-design.md`
- `docs/qnx/history/`
