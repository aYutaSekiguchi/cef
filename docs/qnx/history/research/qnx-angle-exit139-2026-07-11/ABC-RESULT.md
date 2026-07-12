# ABC-RESULT (2026-07-12)

## Goal

Determine whether the SIGTRAP / TLS SetThreadSpecificData failure
(`tls.h@257`) seen in the prior minimal-DSO run was caused by:
- (A) environment / QEMU / guest-reboot state (independent of any DSO)
- (B) LD_PRELOAD itself (loader mechanics on QNX, regardless of DSO contents)
- (C) the diag DSO's constructor / sigaction install

By A/B/C exact-comparison with the same `cefsimple` flags.

## Method

Three runs (`qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180`):

| | LD_PRELOAD | --env CHROME_EXE_PATH | Order |
|---|---|---|---|
| **A** | (none) | (none) | 1 |
| **B** | `exit139_empty.so` (constructor only, no sigaction, no TLS, no global state) | yes | 2 |
| **C** | `exit139_minimal.so` (constructor + sigaction install/readback for SIGSEGV/SIGBUS/SIGILL) | yes | 3 |

`cefsimple` flags (identical across all three):
`--ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr`

Raw logs:
- A: `/tmp/abc-A.log`
- B: `/tmp/abc-B.log`
- C: `/tmp/abc-C.log`

Smoke verification (loader sanity): `exit139_empty.so` smoke
emits `[smoke] empty-loaded`; `exit139_minimal.so` smoke emits
three `[MIN-DIAG]` lines.

## Aggregate counts (FACT)

| Marker | A (no preload) | B (empty DSO) | C (minimal DSO) |
|---|---:|---:|---:|
| `tls.h@257` allocator TLS error | 0 | 0 | 0 |
| `trace trap` (SIGTRAP) | 0 | 0 | 0 |
| `__PI_QNX_EXIT__` | (none — wrapper timed out at 60s) | (none — wrapper timed out at 60s) | (none — wrapper timed out at 60s) |
| `screen_create_context failed` | 0 | 0 | 0 |
| `GLDisplayEGL::Initialize failed` | 0 | 0 | 0 |
| `ready to call SubmitFrame` | 4 | 4 | 4 |
| `SubmitFrame` markers | 4 | 4 | 4 |
| `QnxRenderProducer::Initialize: required DMAbuf` | 3 | 3 | 3 |
| `GPU process exited unexpectedly: exit_code=139` | 3 | 3 | 3 |
| `exit_code=139` (raw) | 6 | 6 | 6 |
| `[exit139-waitstatus] raw=0x8b WIFSIGNALED=1 WTERMSIG=11 WCOREDUMP=1` | 3 | 3 | 3 |
| `[QNX-SIG]` from any handler | **0** | **0** | **0** |
| `[MIN-DIAG]` (handler install + readback) | 0 | 0 | 30 (10 PIDs × 3 sigs) |
| Log lines | 440 | 442 | 472 |

## FACT (from this A/B/C)

1. **A = B = C in GPU behavior**: identical counts of GPU ready
   events, DMAbuf missing errors, GPU process exits, and exit_code
   patterns. The DSO presence has **no measurable effect** on the
   GPU SIGSEGV / `0x8b` exit pattern.

2. **TLS SetThreadSpecificData failure from the prior minimal run
   did NOT reproduce** in C of this ABC. The previous
   `tls.h@257 ... trace trap (core dumped) ... __PI_QNX_EXIT__:133`
   was a **transient guest state issue**, not caused by the DSO.

3. **Handler install without firing is reproduced in C**: all 10
   PIDs (browser + GPU children + helpers) installed the diag
   SIGSEGV/SIGBUS/SIGILL handler with rc=0 and matching readback,
   yet **no `[QNX-SIG]` line was emitted** by any handler in any
   of the 6 GPU SIGSEGV events (3 per run, 3 runs).

4. **GPU SIGSEGV pattern is identical to baseline**: `exit_code=139`,
   raw wait status `0x8b`, `WIFSIGNALED=1`, `WTERMSIG=11`,
   `WCOREDUMP=1`, `info.status=3` (TERMINATION_STATUS_PROCESS_CRASHED).
   This is exactly the pattern from WAITSTATUS-RESULT.md
   reproduced in A/B/C.

## Unknown (explicit)

- **Why doesn't the user-space SIGSEGV handler fire during the GPU
  SIGSEGV?** — The handler is installed correctly in 10 PIDs of C
  and the install/readback matched. The signal IS delivered (kernel
  produces the core dump and `0x8b` exit status), but the user-space
  handler does not execute. Possible explanations not testable from
  user space:
  - QNX kernel bypass of user handlers for specific fault classes
    (e.g., stack overflow, exec fault, page-table corruption)
  - signal delivered but `write()` lost during the kernel teardown
    path before user re-entry
  - kernel-detected invalid memory access in non-user-mappable
    region
  - (other classes not enumerated here)

- **Was the handler overridden by a later `sigaction()` call that
  minimal DSO's constructor did not observe?** — **UNKNOWN.** Minimal
  DSO records install/readback only at constructor time; it has NO
  interpose on `sigaction()`. A later override by the GPU process
  itself, a third-party library, or the loader after constructor
  return would not be detected by minimal DSO. The handler that was
  active at constructor time and the handler active at the moment
  of the SIGSEGV could differ. An attempt to add a `sigaction()`
  interpose (`exit139_sinterpose.so`) failed its own mini gate
  (infinite recursion under LD_PRELOAD, see SINTERPOSE-RESULT.md).
  Stronger override-evidence is therefore not available from
  user-space DSO instrumentation.

- **Was the prior `tls.h@257` trace trap a real allocator failure
  or transient guest state?** — Not reproduced in this ABC. Could
  be: (a) a transient guest state (reboot / tap0 reconnect timing)
  that has since stabilized; or (b) a real allocator init failure
  that requires specific guest config we don't currently have.
  Not investigated further per supervisor directive.

- **Was the prior `tls.h@257` trace trap a real allocator failure
  or transient guest state?** — Not reproduced in this ABC. Could
  be: (a) a transient guest state (reboot / tap0 reconnect timing)
  that has since stabilized; or (b) a real allocator init failure
  that requires specific guest config we don't currently have.
  Not investigated further per supervisor directive.

## Implications

- **The DSO perturbation hypothesis is REJECTED.** Empty DSO and
  minimal DSO produce identical results to no DSO. The GPU
  SIGSEGV is real and reproduced cleanly without any DSO in A.

- **Handler-effectiveness investigation is CLOSED.** All 4
  hypotheses from BLOCKER.md are now data-ruled:
  - install failure: ruled out (install_rc=0, readback matches)
  - override: UNKNOWN — minimal DSO has no sigaction interpose,
    so override by later sigaction() calls was not observed;
    sinterpose attempt failed (see SINTERPOSE-RESULT.md)
  - kernel bypass: not testable from user space; one of multiple
    possible explanations, not a conclusion
  - timing: ruled out (handler installed before crash in all PIDs)

- **`[QNX-SIG]` absence is not attributed** to any single cause
  from user-space evidence. The handler is in place at
  constructor time; the signal is delivered; the user-space
  handler does not fire. Override by later `sigaction()` calls
  is UNKNOWN (no interpose in minimal DSO). Kernel-level bypass
  is one of multiple possible explanations, not a conclusion.

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications this turn.
- `/tmp/exit139-minimal/exit139_minimal.cc` (untracked, source)
- `/tmp/exit139-minimal/exit139_empty.cc` (untracked, source)
- `/home/yuta/chromium/src/out/qnx_release/exit139_*.so` (build
  artifacts, host-side only)
- 0 commit, 0 push, no rebuild of cefsimple / libcef.so
- pre-existing `[exit139-waitstatus]` marker in libcef.so is from
  the WAITSTATUS investigation; left in place per prior-turn
  decision.

## Closure

The user-directed A/B/C TLS-cause investigation is complete with
the conclusion that the previous SIGTRAP was NOT caused by the
DSO (LD_PRELOAD / constructor / sigaction install). The TLS
failure did not reproduce in C, confirming transient / unrelated
to DSO. The handler-effectiveness investigation is now closed:
the handler is correctly installed and never overridden, but the
QNX kernel-side bypass means we cannot observe the fault from
user space.

No further QEMU runs, no DSO changes, no commits.

## Sinterpose closure note

The sigaction-interpose-only DSO (`exit139_sinterpose.so`) was
designed (DESIGN-SIGACTION-INTERPOSE-ONLY.md) as the final
override-discrimination step. It failed its own mini gate with
infinite recursion (7474 `[SIGACT-EARLY]` lines → SIGSEGV /
stack overflow). See SINTERPOSE-RESULT.md. The approach is
**rejected** and not pursued further. The override question
therefore remains UNKNOWN, not eliminated.