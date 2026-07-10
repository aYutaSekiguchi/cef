# QNX ANGLE EGL runtime investigation

- Date: 2026-07-10
- Scope: QEMU virgl, QNX x86_64, Chromium 147 / CEF Phase 7
- Status: investigation only; no ANGLE behavior change applied

## What is already built

The QNX ANGLE patch set intentionally builds the OpenGL backend:

- `ANGLE_PLATFORM_LINUX` is enabled for `__QNX__`.
- `ContextEGL` / `DisplayEGL` and the DMA-buf helper sources are added to the
  GL backend.
- GN metadata for `//third_party/angle:libANGLE` contains
  `ANGLE_ENABLE_OPENGL` and `ANGLE_ENABLE_GL_DESKTOP_BACKEND`.

The port is nevertheless described by its patch as a **minimal Linux/headless
port**. It excludes Linux X11 pixmap/window utility sources. There was no prior
QNX ANGLE display-runtime acceptance evidence.

## Loader collision versus ANGLE runtime

These are separate issues:

1. `libcef.so` directly depends on generated, unversioned `libEGL.so` through
   `//third_party/angle:libEGL`. It can preempt the QNX system Mesa EGL ABI.
   `LD_PRELOAD=/usr/lib/libEGL.so.1` proves the system EGL path fixes native
   CEF's DMAbuf producer and reaches `accepted=true` / `eglSwapBuffers`.
2. Explicit ANGLE selection itself is currently not viable on QNX virgl. This
   remains true without CEF (`content_shell --use-gl=angle`). Therefore simply
   changing CEF's linkage will not by itself make ANGLE usable; it only stops
   ANGLE from breaking the system-EGL path.

## Explicit ANGLE results

| Command suffix | Result |
|---|---|
| `--use-gl=angle --use-angle=gl` | `GLDisplayEGL::Initialize` fails for every display attempt. |
| `--use-gl=angle --use-angle=gles` | Same display-initialization failure. |
| `--use-gl=angle --use-angle=gles-egl` | Reaches the native-EGL ANGLE route, then GPU child aborts: `std::system_error: mutex lock failed: Resource deadlock avoided` (exit 134). |

The `gles-egl` difference is meaningful. Chromium passes
`EGL_PLATFORM_ANGLE_DEVICE_TYPE_EGL_ANGLE`; ANGLE's `Display.cpp` then selects
`rx::DisplayEGL`, whose QNX/Linux path dynamically opens system
`libEGL.so.1`. The plain `gl`/`gles` variants use a default native display;
ANGLE's QNX-as-Linux conditional only instantiates `DisplayEGL` for default
native displays when GBM or Wayland is enabled. QNX has neither, so no display
implementation is selected.

A standalone QNX probe directly linked to generated `out/libEGL.so` confirms
that its client extensions advertise `EGL_ANGLE_platform_angle`, but both
`eglGetDisplay(EGL_DEFAULT_DISPLAY)` and
`eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, ..., OPENGLES)` return
`EGL_NO_DISPLAY` without setting a useful EGL error.

## ANGLE-preserving remediation path

ANGLE support appears feasible but requires a dedicated porting stream:

1. Add a QNX `DisplayEGL` selection path for the default/OPENGLES ANGLE native
   display, rather than limiting it to GBM/Wayland. The `gles-egl` experiment
   proves the underlying system-EGL route is at least selected.
2. Diagnose the `gles-egl` recursive mutex abort. QNX currently falls back from
   ANGLE's Linux futex mutex to `std::mutex`; the error is consistent with
   re-entry into a non-recursive ANGLE mutex. `angle_enable_global_mutex_recursion`
   is a candidate experiment, but a stack trace is required before treating it
   as the fix.
3. Once ANGLE initializes, keep DMAbuf export separate from the ANGLE frontend.
   ANGLE's EGL display will not necessarily expose Mesa's
   `EGL_MESA_drm_image` / `EGL_MESA_image_dma_buf_export`; the producer needs
   a validated native EGL interop/export design.
4. Avoid forcing generated ANGLE `libEGL.so` into every CEF process. Link the
   normal QNX system-EGL path to versioned system `libEGL.so.1`, while retaining
   ANGLE binaries for explicit `--use-gl=angle` runtime selection. This is not
   an ANGLE removal.

## Decision

Do not remove ANGLE. Treat system EGL and explicit ANGLE as two supported
runtime modes, with separate smoke coverage. Do not enable the recursive mutex
flag or change `Display.cpp` until a QNX stack trace identifies the abort site.
