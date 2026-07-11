# QNX ANGLE recursive-mutex investigation (2026-07-11)

- Date: 2026-07-11
- Scope: QNX x86_64, Chromium 147 / CEF qnx_7727
- Status: experimental — patches prepared, build verified, runtime impact
          inferred but not committed.

## Hypothesis confirmed

`std::system_error(EDEADLK)` in chromium's ANGLE GPU child is reproducible
from a tiny standalone QNX test:

```c
#include <mutex>
int main() {
    std::mutex m;
    m.lock();
    try { m.lock(); } catch (std::system_error& e) {
        // e.code().value() == 45 (EDEADLK)
        // e.what() == "mutex lock failed: Resource deadlock avoided"
    }
}
```

Cross-thread contention does NOT raise — `std::mutex::lock()` correctly
blocks. So `std::__2::__tree::find` (which shows up in the post-unwind
trace) only appears because QNX's libgcc_s `_Unwind_Backtrace` returns
the wrong frame; the actual abort site is some EGL entry point that
recursively locks angle::priv::MutexOnStd.

## Fix attempted (rolled back)

Replaced angle::priv::MutexOnStd on QNX with a `pthread_mutex_t`-backed
variant using `PTHREAD_MUTEX_RECURSIVE`. Verified:

- The `qnx_angle_ec_sim.c`-style mutex recursion test returns 0
  instead of EDEADLK when the mutex type is `PTHREAD_MUTEX_RECURSIVE`.
- A small repro `qnx_mutex_test.c` confirms QNX pthread_mutex_t defaults
  to NORMAL (returns EDEADLK on recursion) and RECURSIVE returns 0.
- A standalone `qnx_std_mutex_test.cc` confirms `std::mutex` on QNX
  throws `std::system_error(EDEADLK)` on the same-thread recursive
  case (matching the GPU child abort).
- libEGL still builds end-to-end with the change
  (`cef_create_projects_qnx.sh --clean` && rebuild, ANGLE unit
  translation units compile clean under `-fno-exceptions -fno-rtti
  -std=c++23`).

## Rollback rationale

Smoke test with the patch applied:
```
[QNX-ANGLE-TRACE] std::terminate invoked (pid=610334 tid=1)
[QNX-ANGLE-TRACE] terminate backtrace (1 frames):
[QNX-ANGLE-TRACE] exception: in-flight (rethrow/catch disabled; inspect backtrace above for throw site)
```

The "mutex lock failed: Resource deadlock avoided" message is gone
(proving the MutexOnStd patch is active), but a *different* abort still
fires during ANGLE EGL init. The post-unwind 1-frame trace points
into `std::__2::__tree::find`, which doesn't throw
`std::system_error` itself — meaning the actual throw site is some
allocation/comparator path that we cannot identify without a
backtrace at throw time.

To avoid shipping a fix that changes semantics AND leaves a different
abort in place, the patch was reverted from the chromium tree. The
working piece (`angle_qnx_terminate_capture.patch` +
`new_files/.../terminate_capture_qnx.cc`) remains committed in
`6b8e4c9e8` and `dc8fe3177` as the diagnostic kernel for future
investigation.

## Investigated alternatives

| Alternative | Result |
|---|---|
| Catch in Chromium-side GLDisplayEGL::InitializeDisplay via try/catch | Rejected by user (Chromium style guide prohibits exceptions). |
| Make ANGLE EGL EDEADLK throw a recoverable error (modify MutexOnStd::lock() throw path) | Requires exceptions to recover cleanly; same style-guide problem. |
| Replace angle::SimpleMutex with pthread_mutex_t RECURSIVE (this doc) | Works for the mutex deadlock; a *second* abort remains. |
| Recursive mutex + fix the second abort (likely map allocation failure) | Not attempted — the second-abort site cannot be located without backtraces at throw time. |

## Recommended next step

The remaining gap is *backtrace at throw time on QNX*. To obtain it,
build a separate ANGLE translation unit with `-fexceptions -frtti`
that contains a try/catch around the EGL entry points AND is compiled
into libEGL. That translation unit must NOT use `try`/`catch` in
other places (so it doesn't change other ANGLE source files), only
at the existing EGL dispatch boundary. A patch in the angle source
tree (CEF-managed `angle_qnx_*` series) can do this by adding a new
file compiled with explicit exception-flag overrides.

Once the throw site is known, the underlying bug can be fixed in
ANGLE proper (likely an ANGLE commit upstream in the long run). Until
then, ANGLE's explicit-rendering path remains a known limitation on
QNX; Chromium/CEF on QNX continues to use the system Mesa path via
`LD_PRELOAD=/usr/lib/libEGL.so.1`, which works because System EGL
doesn't have the recursive lock problem (verified in
`docs/qnx/history/research/qnx-ozone-phase7-cefsimple-runtime-diagnostics-2026-07-10.md`).
