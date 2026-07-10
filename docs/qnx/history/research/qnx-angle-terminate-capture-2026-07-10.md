# QNX ANGLE terminate-capture investigation (2026-07-10)

- Date: 2026-07-10..11
- Scope: QEMU virgl, QNX x86_64, Chromium 147 / CEF qnx_7727
- Status: Diagnostic patch in place. Throw-site recovery needs an
          additional wrapping change.

## What changed

- New file: `cef/patch/qnx/chromium/new_files/third_party/angle/src/common/terminate_capture_qnx.cc`.
  Installs a `std::set_terminate` hook at ANGLE-`libEGL` load time via
  `__attribute__((constructor))`. When the Chromium GPU process aborts
  with `std::system_error: mutex lock failed: Resource deadlock
  avoided`, the handler prints a backtrace before propagating.

- New patch: `angle_qnx_terminate_capture.patch`. Adds the .cc file to
  `libangle_common_sources` for `is_qnx` only.  Sequential with the
  existing CEF-managed ANGLE patches; applies cleanly via
  `cef_create_projects_qnx.sh`.

## Smoke observation

```
[QNX-ANGLE-TRACE] terminate handler installed (pid=610334)
[QNX-ANGLE-TRACE] std::terminate invoked (pid=610334 tid=1)
[QNX-ANGLE-TRACE] terminate backtrace (1 frames):
[QNX-ANGLE-TRACE] exception: in-flight (rethrow/catch disabled; inspect backtrace above for throw site)
```

The handler fires and prints "in-flight exception". But the backtrace is
one frame (just the handler itself), because **the libgcc_s
`_Unwind_Backtrace` consumer post-unwind** only sees the handler's
own frame plus a NULL register save, and the **x86_64 frame-pointer
walker** also stops at the same place: the entire stack above
`std::terminate` was unwound before the handler ran.

## Why the throw site is invisible from `std::terminate`

`std::terminate()` is entered after the C++ runtime has already run
exception unwind to `main()` (or to the first enclosing
try/catch). Saved registers for every frame above the handler are
gone. Nothing the handler can do recovers them. This is a known C++
limitation on Linux/QNX alike; not specific to QNX.

ANGLE's EGL entry points (`eglGetPlatformDisplay`, `eglInitialize`,
etc.) live inside the libangle_common translation unit, which Chrome
builds with `-fno-exceptions -fno-rtti`. They cannot catch the
in-flight `std::system_error` thrown from `std::mutex::lock()`
because the catch site requires a try/catch.

## Recommended next step

The throw site can only be captured by **adding a Chromium-side
try/catch wrapper around the ANGLE entry points** that pulls the
mutex deadlock. Concretely:

1. Add a new translation unit compiled with `-fexceptions -frtti`
   that wraps `gl::GLDisplayEGL::InitializeDisplay`'s calls to
   `eglGetPlatformDisplayEXT(...)` / `eglInitialize(...)`. Use
   abseil-style raw `try { ... } catch (std::system_error &e) { ... }`
   around each ANGLE call.

2. On catch, capture the backtrace at the throw site via
   `base::debug::StackTrace` and print the offending `e.code()` (the
   posix `EDEADLK` value confirms the mutex theory) plus the
   `e.what()` string.

3. Re-throw (or convert to a normal `LOG(FATAL)`) so Chromium's
   logging picks it up.

This bypasses ANGLE's no-exceptions constraint because the wrapper
file lives in Chromium's `ui/gl/` (or a similar location) which has
its own exception policy. Without that change the abort site cannot
be located from inside the existing ANGLE sources.

## What the diagnostic still proves

The handler printed `exception: in-flight` and `pid=`/`tid=` for every
aborted GPU child. That alone confirms:

- Every GPU child crash reaches `std::terminate` (not `SIGABRT` from
  the GPU process getting OOB-killed).
- The exception type is one the C++ runtime classifies as
  uncaught-after-unwind (consistent with `std::system_error` from
  `std::mutex::lock`).
- The aborts are reproducible (3 of the first 4 GPU children aborted
  during the smoke; the 4th reached `QnxGpuService::AttachWidget`).

So this is consistent with the Chromium-side mutex theory, but the
**throw site** in ANGLE cannot be located without the wrapper above.

## Patch details

- `cef/patch/patches/qnx/chromium/angle_qnx_terminate_capture.patch`
  applies cleanly to the chromium root after
  `angle_qnx_common_system_utils.patch` (which adds `is_qnx` to the
  `if (is_linux || is_chromeos || is_android || is_fuchsia)` block in
  `third_party/angle/src/libGLESv2.gni`).
- New file via `new_files/third_party/angle/src/common/terminate_capture_qnx.cc`.
- ANGLE's `libangle_common` is built with `-fno-exceptions -fno-rtti`,
  so the file deliberately avoids `try`/`catch` and `typeid()`.
  Mirrors Chromium's own `base::debug::StackTrace` QNX fallback
  (`base/debug/stack_trace_qnx.cc`) using
  `__builtin_frame_address(0)` for the rbp walker.
