# PHASE-B: QNX ANGLE recursive GlobalMutex fix (2026-07-11)

## Scope

Fixes the same-thread re-entry abort on
`egl::priv::GlobalMutex` (ANGLE's EGL global mutex) when using
`--use-gl=angle --use-angle=gles-egl` on QNX, **without** changing
`gl::Context`'s mutex (the context mutex is OUT of scope).

Target: `egl::priv::GlobalMutex` (default non-recursive variant).
Mechanism: enable ANGLE's existing recursive implementation by
defining `ANGLE_ENABLE_GLOBAL_MUTEX_RECURSION=1` in the QNX
`internal_config` block of `third_party/angle/BUILD.gn`. The
recursive class (`GlobalMutex.cpp` line ~78) is unchanged from
upstream; we only set the guard define that selects it.

The define is set directly instead of via the
`angle_enable_global_mutex_recursion` GN arg, because that arg
also enables `ANGLE_ENABLE_CONTEXT_MUTEX_RECURSION` (which would
change `gl::Context`'s mutex semantics) — explicitly OUT of scope
per supervisor direction.

## CEF-managed patch

`patch/patches/qnx/chromium/angle_qnx_global_mutex_recursion.patch`
(machine-generated via `/tmp/angle_rec_patchroot` clean baseline +
modified overlay + `git diff --no-prefix --relative --full-index`,
no hand-written header/hunk).  Single hunk, 7 added lines:

```
+  # QNX: enable ANGLE's recursive GlobalMutex implementation so that
+  # same-thread re-entry on egl::priv::GlobalMutex does not abort
+  # (commit 36988aca confirmed 2/2 GPU child re-entry).
+  if (is_qnx) {
+    defines += [ "ANGLE_ENABLE_GLOBAL_MUTEX_RECURSION=1" ]
+  }
```

`patch.cfg` registers it after `angle_qnx_throw_tracer`.

## Verification

### Build + bootstrap

- `qnx-bootstrap` clean recipe (`git checkout -f` → `gclient sync -f -R`
  → `cef/tools/cef_create_projects_qnx.sh --build-type Release`):
  **489 patches (468 applied, 21 skipped, 0 failed)**, GN gen
  `Done. Made 33073 targets`.
- `ninja libGLESv2` — rc=0, 319/319 actions.
- `ninja third_party/angle/src/tests:angle_unittests` — rc=0,
  1200/1200 actions.

### Unit test (angle_unittests)

`angle_unittests --gtest_filter=GlobalMutexTest.*` on QEMU virgl:

```
[----------] 5 tests from GlobalMutexTest
[ RUN      ] GlobalMutexTest.ScopedGlobalEGLMutexLock
[       OK ] GlobalMutexTest.ScopedGlobalEGLMutexLock (10620 ms)
[ RUN      ] GlobalMutexTest.ScopedOptionalGlobalMutexLockEnabled
[       OK ] GlobalMutexTest.ScopedOptionalGlobalMutexLockEnabled (10445 ms)
[ RUN      ] GlobalMutexTest.ScopedOptionalGlobalMutexLockDisabled
[       OK ] GlobalMutexTest.ScopedOptionalGlobalMutexLockDisabled (6 ms)
[ RUN      ] GlobalMutexTest.RecursiveScopedGlobalEGLMutexLock
[       OK ] GlobalMutexTest.RecursiveScopedGlobalEGLMutexLock (0 ms)
[ RUN      ] GlobalMutexTest.RecursiveScopedOptionalGlobalMutexLock
[       OK ] GlobalMutexTest.RecursiveScopedOptionalGlobalMutexLock (0 ms)
[----------] 5 tests from GlobalMutexTest (21087 ms total)
[  PASSED  ] 5 tests.
```

**5/5 PASSED**, exit 0.  Includes the `Recursive*` tests that
exercise the recursive class.  No new semantic failure introduced.

Raw log: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/angle-unittests-globalmutex.log`

### Runtime smoke (cefsimple)

Command: `cefsimple --ozone-platform=qnx --use-gl=angle
--use-angle=gles-egl --no-sandbox --use-native --url=about:blank
--ozone-qnx-gpu-trace --enable-logging=stderr`

Raw log: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/phaseB-final-cefsimple-smoke.log`

### Before / After

| Marker                                    | Before (36988aca) | After (this fix) |
|-------------------------------------------|-------------------|-------------------|
| `std::terminate invoked` events            | 2                 | **0**             |
| GPU child `exit_code=134`                 | 1                 | **0**             |
| `QnxGpuService::Initialize: ready to call SubmitFrame` reached | no  | **yes (2 markers)** |
| `BindGpuControlAndAttachExistingWidgets: Initialize ack received` | no | **yes** |

The runtime now advances past the previous abort point: the GPU
process reports `gpu_host_remote bound; GPU process is ready to call
SubmitFrame`, which was unreachable in the 36988aca baseline.

## Residual problem (separate from this fix, not addressed)

`exit_code=139` (SIGSEGV) is now observed where `exit_code=134`
was previously observed.  The recursive mutex abort is gone, but a
downstream `QnxRenderProducer::Initialize` reports missing DMAbuf
export extensions, and `DisplayEGL::generateConfigs` logs many
`RGBA(N,N,N,0) not handled` warnings.  These are separate
capability/runtime issues (ANGLE/platform/extension probe), not
mutex-semantic failures.  Per supervisor direction, no speculation
on root cause or extension list — recorded as a separate problem
that needs its own investigation.

## Removed from CEF durable source (after Phase A success)

The GMD (GlobalMutex Diagnostic) in-process instrumentation from
commits `36988aca` and `1aa7f5cbb` was successfully used to
confirm the same-thread re-entry hypothesis, then removed from the
CEF durable source because it was diagnostic-only and is no longer
needed once the recursive fix is in place:

- `patch/patches/qnx/chromium/angle_qnx_global_mutex_diag.patch` (deleted)
- `patch/patches/qnx/chromium/angle_qnx_global_mutex_diag_sources.patch` (deleted)
- `patch/patches/qnx/chromium/angle_qnx_global_mutex_diag_build.patch` (deleted)
- `patch/qnx/chromium/new_files/third_party/angle/src/libANGLE/global_mutex_qnx_diag.cc` (deleted)
- 3 entries in `patch.cfg` (deleted)

The chromium tree was discarded via the clean recipe; no GMD
artifacts remain in `third_party/angle/`.  `/tmp/angle_rec_patchroot`
was removed after machine-generation completed.

## Related docs

- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/RESULT.md`
  — GMD-based same-thread re-entry confirmation (36988aca).
- `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/FACT-CORRECTION.md`
  — explicit retraction list (1aa7f5cbb).
- `docs/qnx/history/research/qnx-angle-throw-tracer-smoke-2026-07-11.md`
  — original LD_PRELOAD tracer smoke (now SUPERSEDED).
- `docs/qnx/history/research/qnx-angle-throw-tracer-design-2026-07-11/DESIGN.md`
  — original LD_PRELOAD tracer design (not retracted; useful for
  ANGLE/libGLESv2 frames within unwinder limitations).