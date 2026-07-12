# TLS-SIGTRAP-NOTE (2026-07-12)

## Status

Tracking paused. Recorded as observation only. **No further TLS
experiments, reproduction attempts, or allocator/kernel analyses
are authorized at this time** per user decision. Tracked when
reproduction frequency increases.

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

## UNKNOWN

- **Direct cause of the `tls.h@257` SetThreadSpecificData failure.**
  Not determined. One observation (run2 of the previous session).
- **Whether the failure is DSO-related, guest-state-related, or
  transient allocator init noise.** A/B/C runs in the same session
  did not reproduce. minimal DSO ELF has no TLS dependencies.
- **Whether the failure correlates with guest boot state, tap0
  reconnect timing, or some other environmental variable** not
  currently instrumented.
- **Reproducibility under what specific conditions.** The single
  observation cannot be characterized as "always happens" or
  "rarely happens".

## Tracking policy

- **Tracking is paused.** No further TLS experiments,
  reproductions, or allocator/kernel analyses are authorized.
- The observation is recorded here for future reference in case
  reproduction frequency increases.
- If a future run shows the same `tls.h@257 ... trace trap ...
  __PI_QNX_EXIT__:133` pattern, this note should be reopened
  and the new observation compared against this baseline.

## Cross-references

- `/tmp/exit139-minimal-cefsimple-run2.log` — single observation
- `/tmp/abc-{A,B,C}.log` — A/B/C runs that did NOT reproduce
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/HANDLER-EFFECTIVENESS-RESULT.md`
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/ABC-RESULT.md`
- `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/SINTERPOSE-RESULT.md`

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications.
0 commit, 0 push.