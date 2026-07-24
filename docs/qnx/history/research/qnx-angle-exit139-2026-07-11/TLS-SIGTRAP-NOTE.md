# TLS-SIGTRAP-NOTE (2026-07-12, resolved 2026-07-24)

## Status

Resolved. The investigation was reopened on 2026-07-23 after the failure
recurred. A deterministic reproducer, QNX core backtrace, durable managed
patch, and post-fix QEMU validation are recorded in:

`docs/qnx/history/build-errors/test/runtime-assumption/qnx-heap-profiler-pthread-tls-sigtrap.md`

## FACT

1. **2026-07-12 minimal DSO + `--virgl` run** (`/tmp/exit139-minimal-cefsimple-run2.log`,
   49 lines, run wall time ~35s):
   - `[MIN-DIAG] sig=SIGSEGV/SIGBUS/SIGILL install_rc=0 readback_rc=0`
     for 2 PIDs (browser sh-wrapper + 1 child).
   - Browser log line:
     `TLS System: Failed to set thread specific data. Failed
     condition 'tls_system.SetThreadSpecificData(slot)' in
     (../../base/allocator/dispatcher/tls.h@257).`
   - QNX shell output: `trace trap    (core dumped) sh -c '...cefsimple...'`
   - Wrapper exit: `__PI_QNX_EXIT__:133` (SIGTRAP = signal 5).
   - No GPU path reached: `ready=0`, `SubmitFrame=0`, `DMAbuf=0`,
     `GLDisplayEGL::Initialize failed=0`, `screen_create_context
     failed=0`, `exit_code=139=0`, `[QNX-SIG]=0`.
   - Source: `chromium/src/base/allocator/dispatcher/tls.h:257`
     (allocator dispatcher TLS slot setup).

2. **A/B/C runs** (same day, same `qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180`,
   same `cefsimple` flags, order A → B → C):
   - A (no LD_PRELOAD): `tls.h@257=0`, `trace trap=0`, no
     `__PI_QNX_EXIT__` (wrapper timed out at 60s).
   - B (empty DSO): same as A — `tls.h@257=0`, `trace trap=0`.
   - C (minimal DSO): same as A — `tls.h@257=0`, `trace trap=0`.
   - All three reached `ready=4 / SubmitFrame=4 / DMAbuf=3 /
     gpu_exit139=3 / exit_code=139=6 / [exit139-waitstatus] raw=0x8b=3`.
   - GPU SIGSEGV baseline reproduced cleanly in all three.

3. **minimal DSO ELF / source TLS audit** (2026-07-12, read-only
   via `readelf` / `nm`):
   - Source `grep -nE 'thread_local|__thread|tls_|TLS'` = 0 hits.
   - Source `grep -nE 'pthread_self|gettid'` = 0 hits.
   - ELF: no `.tdata`, no `.tbss`, no `PT_TLS` segment, no
     `__tls_get_addr` symbol (static or dynamic), no TLS-related
     dynamic tags.
   - bss globals only: `g_real_sigaction` (8B), `g_in_handler` (4B).
   - **The minimal DSO has no TLS dependencies.**

## Resolution of the former unknowns

- The direct cause is allocator-shim reentry while QNX
  `pthread_setspecific` lazily reallocates per-thread key storage.
- The QNX `ENOMEM` return is produced by that recursive allocation path; it
  is not evidence of system-wide OOM.
- The minimal DSO was incidental and contains no TLS dependency.
- `HeapProfilerReporting:stable-probability/1.0` made the old failure
  deterministic. Its normal 1% stable-channel probability explained the
  earlier intermittent observation.
- QNX heap-profile collection is now disabled internally while preserving the
  `HeapProfilerController` object required by child-process clients.

## Tracking policy

If a future run shows the same `tls.h@257 ... trace trap ...
__PI_QNX_EXIT__:133` pattern, first verify that the deployed `libcef.so`
contains the managed QNX heap-profiler fix. The old
`--disable-features=HeapProfilerReporting` switch is only a workaround for
pre-fix binaries.

## Cross-references

- `/tmp/exit139-minimal-cefsimple-run2.log` — single observation
- `/tmp/abc-{A,B,C}.log` — A/B/C runs that did NOT reproduce
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/HANDLER-EFFECTIVENESS-RESULT.md`
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/ABC-RESULT.md`
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/SINTERPOSE-RESULT.md`

## Tracked source state

The resolution is carried by the CEF-managed patch named
`qnx/chromium/components_heap_profiler_disable_collection_qnx`.
