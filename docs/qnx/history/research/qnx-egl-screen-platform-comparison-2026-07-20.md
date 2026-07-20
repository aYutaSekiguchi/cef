# QNX EGL default-display vs Screen-platform comparison (2026-07-20)

## Goal

Compare Chromium's current `eglGetDisplay(EGL_DEFAULT_DISPLAY)` path with the
explicit `EGL_PLATFORM_SCREEN_QNX` path used by the qnx-ports Weston backend.

## Method

`qnx_screen_egl_window_probe` was extended with `--platform-screen`. Both modes
use the same Screen window, EGL window surface, GLES2 context, clear, swap, and
cleanup sequence. Each mode was run in a fresh QEMU virgl guest process.

## Results

| Display path | Result | Runtime details |
|---|---|---|
| `eglGetDisplay(EGL_DEFAULT_DISPLAY)` | PASS | Mesa EGL 1.5; window surface, context, clear, swap, and cleanup passed |
| `eglGetPlatformDisplayEXT(EGL_PLATFORM_SCREEN_QNX, EGL_DEFAULT_DISPLAY, NULL)` | FAIL | Returned `EGL_NO_DISPLAY`; `eglGetError()` remained `EGL_SUCCESS` |
| Explicit Screen platform with `/usr/lib/libEGL.so.1` preloaded | FAIL | Same `EGL_NO_DISPLAY` result |

The extension inventory explains the result:

- `EGL_EXT_platform_base`: present
- `eglGetPlatformDisplayEXT`: resolved
- `EGL_QNX_platform_screen`: absent
- `eglGetPlatformDisplayQNX`: not found
- `EGL_QNX_image_native_buffer`: present

The QNX SDP header defines `EGL_PLATFORM_SCREEN_QNX`, but the QEMU Mesa EGL
runtime does not advertise or implement that platform. Function-pointer
availability therefore does not imply support for every platform enum.

Runner transcripts:

- `out/qnx_release/qnx_run_20260720_183002_qnx_screen_egl_window_probe.log.serial`
- `out/qnx_release/qnx_run_20260720_183034_qnx_screen_egl_window_probe.log.serial`
- `out/qnx_release/qnx_run_20260720_183112_qnx_screen_egl_window_probe.log.serial`
- `out/qnx_release/qnx_run_20260720_183136_qnx_egl_extension_probe.log.serial`

None of these standalone runs emitted
`qs_destroy_loader_image_state(): LoaderPrivate argument is not NULL`. The
successful probe creates a window surface and context but does not exercise
Chromium's shared-image lifecycle, so this does not disprove the warning seen
in `cefsimple`.

## Conclusion

Do not replace the QEMU path with an unconditional
`EGL_PLATFORM_SCREEN_QNX` call. A real-hardware implementation may probe the
client extension string and use the explicit Screen platform only when
`EGL_QNX_platform_screen` is advertised, retaining
`eglGetDisplay(EGL_DEFAULT_DISPLAY)` as the fallback.

Follow-up isolation proved that `eglCreateDRMImageMESA` alone is sufficient to
trigger the warning during correct `eglDestroyImageKHR` cleanup. See
`qnx-mesa-loaderprivate-warning-isolation-2026-07-20.md`. Display selection is
not a viable A/B fix in this runtime.
