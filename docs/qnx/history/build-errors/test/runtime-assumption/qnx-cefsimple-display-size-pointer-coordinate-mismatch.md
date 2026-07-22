# QNX cefsimple display size and pointer coordinates diverge

- Date: 2026-07-22
- Signature: compositor reports 1280x768 while the Screen window/export buffer remains 1004x748 or 1050x748; physical pointer does not reach DOM targets
- Stage: test
- Category: runtime-assumption
- Scope: QNX Screen display enumeration / PlatformWindow bounds / GPU producer resize / cefsimple input

## Symptoms

`cefsimple` displayed page content, but the right side of the 1280x768 QEMU
display remained black. QNX desktop and sample applications could draw in that
region. The physical mouse generated QNX Screen pointer events, yet clicks did
not reliably reach the web page DOM.

The sizes used by the stack disagreed:

- `QnxScreen` advertised a hard-coded 1024x768 display.
- QNX Screen exposed an actual 1280x768 display.
- maximizing changed Chromium state without resizing the native Screen window.
- the GPU-side `QnxRenderProducer` retained the initial window size even after
  the browser-side native window bounds changed.

This left compositor, native-window, exported-buffer, and input hit-test
coordinates referring to different rectangles.

## Root cause

The QNX Ozone implementation still contained bring-up placeholders in three
connected paths:

1. `QnxScreen` did not enumerate `SCREEN_PROPERTY_DISPLAYS` or read each
   display's position and size.
2. `QnxWindow::Maximize()` and `SetFullscreen()` only changed window state;
   they did not apply the selected display bounds to the Screen window.
3. `QnxWindowManager::UpdateWidgetSize()` updated its browser-side record but
   did not notify `QnxGpuPlatformSupportHost`, so the GPU producer continued
   allocating/exporting frames at the old size.

`SetBoundsInPixels()` also compared the new origin after overwriting
`bounds_`, causing `origin_changed` to remain false.

## Fix pattern

- Enumerate attached QNX Screen displays and construct Chromium `Display`
  objects from `SCREEN_PROPERTY_POSITION`, `SCREEN_PROPERTY_SIZE`, and
  `SCREEN_PROPERTY_ID`; retain 1024x768 only as an error fallback.
- Apply target display or work-area bounds when entering fullscreen or
  maximized state, and preserve the prior bounds for restore.
- Forward native size changes through `QnxWindowManager` and
  `QnxGpuPlatformSupportHost` to `QnxGpuControl.ResizeWidget()`.
- Compare the old and new bounds before notifying the platform-window
  delegate.
- Keep a DOM-level coordinate probe with edge and sub-16-pixel targets in the
  normal cefsimple payload.

## Verification

Build:

```text
./out/qnx_release/ninja_qnx.sh cefsimple
[.../...] SOLINK ./libcef.so
exit 0
```

QEMU virgl runtime with `--start-maximized`:

```text
QnxScreen display bounds       1280x768
compositor/export buffer       1280x768
imported/displayed frame       1280x768
black right-side strip         absent
physical mouse DOM interaction confirmed
```

The user confirmed that the same physical mouse which previously produced no
web interaction now operates the probe page. The probe logs DOM events and
target hits using the `[QNX_DOM_PROBE]` prefix. It includes 48x48, 16x16, 8x8,
and 4x4 targets, four edge targets, and 2-pixel strips for later regression
checks.

The coordinate probe continues to prevent the default context menu so its
small-target measurements remain isolated. The separate context-menu SIGSTOP
was subsequently resolved by parenting menu windows through a QNX Screen
window group; see the related context-menu note below.

## Files touched

- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_screen.{cc,h}`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/qnx_window.{cc,h}`
- `patch/qnx/chromium/new_files/ui/ozone/platform/qnx/ozone_platform_qnx.cc`
- `patch/patches/qnx/chromium/qnx_window_resize_gpu_producer.patch`
- `patch/patch.cfg`
- `tests/cefsimple/qnx_input_probe.html`
- `tests/cefsimple/README.md`
- `tools/qnx_payload_http.sh`

## Related notes

- `qnx-clean-build-pointer-events-stop-before-native-view-host.md`
- `qnx-clean-build-black-uninitialized-test-frame.md`
- `qnx-cefsimple-search-hascapture-trap.md`
- `qnx-cefsimple-context-menu-unparented-window-sigstop.md`
