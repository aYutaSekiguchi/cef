# QNX ANGLE same-thread same-GlobalMutex re-entry: RESULT (2026-07-11)

## Scope

`cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native`
under QEMU virgl, in-process GlobalMutex diagnostic instrumentation
(`ANGLE_QNX_GLOBAL_MUTEX_DIAGNOSTIC=1`).  No LD_PRELOAD in this run.

## Verdict

**CONFIRMED (2/2 terminate events)**: same-thread, same-GlobalMutex,
non-recursive re-entry on a GlobalMutex instance (slot identity not
determined; see Unknown section) during ANGLE EGL initialization causes
`std::terminate` (exit_code=134) in both observed GPU child processes.

判定条件 (supervisor-specified) の同一 run 内合致:

| pid  | tid | mutex A (`this`)    | mutex B (`this`)    | REENTRY on A | terminate |
|------|-----|----------------------|----------------------|--------------|-----------|
| 614431 | 1 | 0x5c00004230          | 0x5c000042a0          | YES (before B's unlock) | YES (tid=1) |
| 708639 | 1 | 0x3c00074210          | 0x3c00074280          | YES (before B's unlock) | YES (tid=1) |

Event sequence (pid=614431, last 8 events before terminate):

```
L80 [lock-entry    ] this=0x5c00004230 was_held=0 total_held=0
L82 [unlock-entry  ] this=0x5c00004230 was_held=1 total_held=1
L84 [lock-entry    ] this=0x5c00004230 was_held=0 total_held=0
L86 [unlock-entry  ] this=0x5c00004230 was_held=1 total_held=1
L88 [lock-entry    ] this=0x5c00004230 was_held=0 total_held=0
L90 [lock-entry    ] this=0x5c000042a0 was_held=0 total_held=1   ← mutex B starts
L92 [lock-entry    ] this=0x5c00004230 was_held=1 total_held=2   ← mutex A re-locked
L93 [REENTRY       ] this=0x5c00004230 was_held=1                ← same-mutex same-thread
L94 [QNX-ANGLE-TRACE] std::terminate invoked (pid=614431 tid=1)
```

## Distinction: re-entry vs nested-different-mutex

Earlier interpretation (v4c + fc8b36673 docs) suggested "second mutex
holds while first still held ⇒ re-entry".  That interpretation is wrong:

- **mutex A (`0x5c00004230`)** and **mutex B (`0x5c000042a0`)** are
  DIFFERENT objects (different `this` addresses, difference `0x70 = 112`
  bytes).
- The pattern that triggers REENTRY is **lock(A) → lock(B) → lock(A)**,
  i.e. **same `this` re-entered**, not two distinct mutexes held
  simultaneously.
- The terminate fires inside the `mMutex.lock()` of the re-entered A call,
  because the underlying `std::mutex` is non-recursive and the same
  thread already holds it (held=1 before the second lock attempt).

This explains why a recursive `std::mutex` (pthread_mutex_t PTHREAD_MUTEX_RECURSIVE)
on a non-ANGLE `MutexOnStd` instance would not have addressed the abort: the
recursive fix would need to be applied to ANGLE's `egl::priv::GlobalMutex`
itself, which is what `ANGLE_ENABLE_GLOBAL_MUTEX_RECURSION` does (not used in
this build). The `MutexOnStd` experiment referenced in earlier fc8b36673 docs
is a separate class (`angle::priv::MutexOnStd`) and is NOT the GlobalMutex
instrumented by this commit.

## FACT vs SPECULATION

### FACT (same-run evidence, single smoke, two GPU child pid)

- 44 [GMD] events emitted across 2 distinct GPU child pids (614431, 708639).
- Each GPU child observed exactly one REENTRY event on the same pid/tid=1.
- Each REENTRY event is immediately followed by `std::terminate invoked
  (pid=X tid=1)` and the GPU process exits with `exit_code=134`.
- Both GPU children observe the same pattern: lock(A) → lock(B) →
  re-lock(A) → REENTRY → terminate.
- mutex A and mutex B have different `this` addresses (different
  GlobalMutex objects).
- `libGLESv2.so+0x9eb85` corresponds to the `EGL_GetDisplay` function
  (size 128, `entry_points_egl_autogen.cpp:498`).  Three independent
  confirmations: `nm -C` symbol table, `objdump` disassembly (instruction
  at `0x9eb80` calls `ScopedGlobalMutexLock<0>::ScopedGlobalMutexLock()`
  constructor), and `addr2line` on the unstripped binary.  Adopted
  individually as a structural hint; not a throw-site evidence (this
  raw log captures REENTRY only, not the throw frame).

### SPECULATION (not proven by this smoke)

- The two distinct mutexes A and B are `g_Mutex` (EGL global) and
  `g_EGLSyncMutex` (EGL Sync global).  This is highly plausible given
  ANGLE's GlobalMutex.cpp has exactly two static mutex pointers, but the
  diag does NOT record which slot each `this` belongs to.  The `g_Mutex`
  / `g_EGLSyncMutex` assignment can be confirmed by adding a slot
  tag in the diag TU, but that would be a new patch and is OUT of scope
  for this RESULT.
- The call chain leading to the second lock(A) is the same code path
  every iteration of the GPU init (we see the lock(A)→unlock(A)→lock(A)
  pattern repeated 11 times before the final nested sequence).  Not
  proven; just observation.
- The "mutex lock failed: Resource deadlock avoided" message that is
  reported by `std::terminate` after the REENTRY event is consistent
  with `std::mutex::lock()` aborting when the calling thread already
  owns the mutex, but the FACT scope of this RESULT is limited to
  REENTRY immediately preceding `std::terminate invoked` and
  `exit_code=134`.  Whether the exact `std::system_error(EDEADLK)`
  message was emitted is not observed in this run's raw log (the
  terminate handler captured only 1 post-unwind frame, which we do not
  use here).

### UNKNOWN / NOT OBSERVED

- The throw's exact call site within `mMutex.lock()` (i.e. which
  internal call into `__throw_system_error` fires).  The throw-tracer
  v4c smoke (committed in fc8b36673) gave an `__throw_future_error`
  symbol which was retracted as unwinder unreliable for libc++ caller
  frames; the throw-tracer is not used in this run.

## LD_PRELOAD throw-tracer (fc8b36673) — current status

The committed `libangle_throw_tracer.so` (LD_PRELOAD) is **not used in
this smoke** (binary artifact not committed; would require a separate
host build).  If it were used together with the in-process GlobalMutex
diag, the correlation between REENTRY and the throw's call site would
be observable in a single run.

The two tools are independent:
- `libangle_throw_tracer.so` (LD_PRELOAD, fc8b36673): records `__cxa_throw`
  calls with raw RAs; unreliable for libc++ frames per earlier audit
  (`v4c-correlate-symbolize.txt`); useful for ANGLE/libGLESv2 frames.
- in-process GlobalMutex diag (this commit, qnx-bootstrap clean apply):
  records lock/unlock/REENTRY events with pid/tid/this; operates in
  the same address space so no race or propagation issue.

For this smoke the in-process diag is sufficient to confirm the
supervisor's判定条件.  The throw-tracer was deliberately not used here
to keep the smoke minimal.

## Reproducibility

Run from chromium root:

```bash
./out/qnx_release/ninja_qnx.sh libGLESv2
./cef/tools/qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 -- \
  './cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1 & p=$!; sleep 15; kill -TERM $p 2>/dev/null || true; wait $p 2>/dev/null || true; exit 0'
```

Raw log: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/cefsimple-angle-gles-egl-gmdiag.log`

Parser: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/scripts/parse_gmd.py`

Parsed output: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/cefsimple-angle-gles-egl-gmdiag.parse.txt`

## Build artifacts NOT committed

- `out/qnx_release/libGLESv2.so` (20007944 bytes, contains diag TU)
- `out/qnx_release/libEGL.so`, `out/qnx_release/cefsimple` etc.

Per project policy, build artifacts stay out of git.  The diag is
recorded via the in-process [GMD] lines written to stderr, which ARE
captured in the smoke log.

## Next step (supervisor direction)

The fc8b36673 commit added the LD_PRELOAD throw-tracer with conclusions
that are partially retracted by this RESULT:

- "same-thread re-entry on global mutex" is CONFIRMED for two GPU
  child processes in this smoke, with REENTRY→terminate→exit134 sequence
  matching the supervisor's判定条件.
- "throw site in std::mutex::lock() of GlobalMutex" is STRONGLY INFERRED
  from disassembly (`EGL_GetDisplay` → `ScopedGlobalMutexLock` ctor → `mMutex.lock()`)
  but the throw-tracer v4c run did not capture it cleanly due to
  QNX `_Unwind_Backtrace` unwinder limitations.

A correction document for fc8b36673 should be added (or this RESULT
should be cited as the correction).  Decision pending supervisor.