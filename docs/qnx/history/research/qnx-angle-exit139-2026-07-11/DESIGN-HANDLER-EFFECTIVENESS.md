# DESIGN-HANDLER-EFFECTIVENESS (2026-07-11)

## Goal

Determine why the diag DSO's signal handler did not fire (`[QNX-SIG]`
absent in the instrumented cefsimple run) given that the GPU child
crashed with a real SIGSEGV (WIFSIGNALED=1, WTERMSIG=11, WCOREDUMP=1)
per `WAITSTATUS-RESULT.md`.

Candidates to discriminate:
- (a) `sigaction()` install returned non-zero (failed install)
- (b) Handler address did not match our expected diag handler
  (loader / symbol-binding issue)
- (c) A subsequent `sigaction()` call overrode our handler
- (d) Handler did not run because of timing (signal delivered
  before handler was installed, or in a different thread)
- (e) Handler ran but `write()` output was dropped by QEMU
  serial/stdio buffering
- (f) Handler ran but a downstream `abort()` / `_exit()` / `_exit(139)`
  killed the process before our write reached stderr

The existing DSO source `/tmp/exit139-diag/exit139_diag.cc` was removed
at end of prior turn (Phase E cleanup per supervisor). It must be
recreated for this investigation. No tracked source change, no commit.

## Established FACT (from prior turns)

| | FACT | Source |
|---|------|--------|
| GPU child crash | real SIGSEGV (WIFSIGNALED=1, WTERMSIG=11, WCOREDUMP=1) | WAITSTATUS-RESULT.md, 2 runs |
| Browser-reported exit_code | 139 (= raw 0x8b printed as %d) | WAITSTATUS-RESULT.md |
| QNX-SIG absent | diag handler did not emit `[QNX-SIG]` line in either instrumented run | BLOCKER.md, GATE-RUN-RESULTS.md |
| diag handler install rc unknown | source does not capture `sigaction()` return value | BLOCKER.md (4) |

## Phase A design: DSO modifications (no Chromium/CEF tracked change)

### (1) Constructor: capture sigaction() install rc and readback active handler

In the diag constructor, after each `sigaction(sig, &sa, NULL)` call:
- Capture the return value (`int rc = sigaction(...)`) and emit a
  raw normal-context line: `[diag-sigaction] op=install sig=N
  rc=N`. `rc == 0` = success, `rc != 0` = failure (errno detail not
  needed for this gate).
- Immediately read back the active handler via
  `sigaction(sig, NULL, &old)`: emit `[diag-sigaction] op=readback
  sig=N handler=<hex address of sa_sigaction field>`. This catches
  case (b) (loader binding) and case (c) (someone else installed
  later) at runtime, not via static audit.

### (2) DSO interpose: `sigaction()` (NOT `signal()`, NOT `waitpid`)

Interpose `sigaction()` to observe every subsequent install
(whether from our diag, another DSO, a Chromium base function, or
even a test). For each call, log:

- `signal` (SIGSEGV / SIGBUS / SIGILL / other)
- `rc` (return value)
- `new_handler` (handler address being installed)
- `caller_ra` (return address: the call site of sigaction)
- `old_handler_present` (was `oldact` non-NULL on input?)

`signal()` is a separate historical API not used on POSIX 2008+ for
these signals; we do not interpose it.

The interpose MUST:
- Resolve the real `sigaction` via `dlsym(RTLD_NEXT, "sigaction")`
  in a constructor that runs BEFORE the interpose is active (same
  pattern as the existing `dlopen` interpose gate).
- Use a recursion guard (`thread_local int sigaction_depth`) so the
  loader's own `sigaction` calls do not recurse.
- Forward the call UNCHANGED. We do NOT alter the handler or the
  action; only observe.
- Capture the `caller_ra` via `__builtin_return_address(0)` —
  async-signal-safe per signal-safety(7) for `__builtin_return_address`
  with literal 0 in a normal (non-signal) context.
- Output is normal-context (the interpose is in normal user code,
  not a signal handler). `fprintf(stderr, ...)` is fine.

### (3) Signal handler itself is unchanged

The existing `handler` is async-signal-safe. We do NOT add `write`
or `snprintf` calls to it. It already emits `[QNX-SIG] pid=...
tid=... sig=N ... PC=... SP=...` via the existing raw-write bounded
format. **No change to the handler body.** This means:
- If the handler runs, we get the same `[QNX-SIG]` line as before.
- If the handler does NOT run, we can discriminate via:
  - Did `sigaction` succeed in the constructor? (1)
  - Did a later `sigaction` override ours? (2)
  - Was our handler address still active at the time of the crash?

### (4) QNX-SIG absent does NOT imply non-signal exit

The previous baseline instrumented run had no `[QNX-SIG]` but DID
have `0x8b` raw wait status (WIFSIGNALED=1, WTERMSIG=11). We do NOT
interpret the absence of `[QNX-SIG]` as "the crash was not a signal".
A real SIGSEGV can still fail to reach our handler if:
- Our `sigaction()` failed at install
- Our handler was overridden later by another `sigaction()`
- The signal was delivered to a thread that did not have our handler
  installed (sigaction handlers are process-wide on POSIX, but
  per-thread masking can block delivery — though SIGSEGV cannot
  be masked to pending in normal POSIX; SA_NODEFER is not set
  on our handler so on-entry mask inherits blocked signals, but
  SIGSEGV/SIGBUS/SIGILL are not blockable to begin with on QNX)

### (5) Failure conditions and recovery

If the new sigaction-interpose causes:
- Gate failures (LOADED missing, MAP missing, QNX-SIG missing,
  WIFSIGNALED not detected, MAP-FAIL): STOP, no commit.
- Build failure: build-breakage-loop per prior convention; first
  actionable error only.
- Loader recursion in our own sigaction resolution: STOP.
- A new crash or different crash pattern: STOP, no commit.

Recovery: undo the source-only edits in `/tmp/exit139-diag/exit139_diag.cc`
before this turn, no CEF/Chromium tracked changes. No commit.

## Phase B/C design: same launch form as prior turns

- Build only the modified DSO with `q++ -Vgcc_ntox86_64`.
- Re-run mini gate and inheritance gate to confirm:
  - `LOADED` from constructor
  - MAP lines from both constructor and (if applicable) dlopen
  - `QNX-SIG` from deliberate SIGSEGV trigger
  - `WIFSIGNALED` / `WTERMSIG=11` (mini gate would crash with signal 11)
  - No `MAP-FAIL`
  - Constructor `sigaction` rc=0 for SIGSEGV/SIGBUS/SIGILL
  - Sigaction interpose log entries visible (proves it was called)
- Then `qnx_run.sh --env LD_PRELOAD=... --env CHROME_EXE_PATH=cefsimple
  -- /mnt/nfs/out/qnx_release/cefsimple ...` (per the run-3 launch
  form that worked), 1 run only, save raw to
  `/tmp/exit139-handler-effectiveness-run1.log`.

## Phase D design: PC symbolization (only if QNX-SIG is present)

If the instrumented run produces a `[QNX-SIG]` line:
- Use the most recent `[MAP]` from the same PID (diag emits a fresh
  MAP per `dlopen` call, plus constructor)
- Map the raw PC to DSO + offset via the MAP base/end ranges
- Verify via `nm -C`, `objdump -d`, `addr2line` (3-point check)
- Adopt only when ≥2 of 3 agree

If `QNX-SIG` is still absent in this run, the result is:
"QNX-SIG absent cannot be attributed to any single cause without
runtime evidence of which candidate (a/b/c/d/e) is responsible."
That is an UNKNOWN outcome; do NOT speculate on cause. No further
runs, no further code changes.

## Out of scope (per supervisor prior guidance)

- No CEF/Chromium tracked source change
- No permanent fix
- No commit / no push
- `git checkout`/`git reset` not used to revert; only direct edit
  of `/tmp/exit139-diag/exit139_diag.cc`
- No `kill_posix.cc` or other Chromium file change in this turn
  (the wait-status diagnostic from the prior turn is reverted
  already and not part of this investigation)
- `signal()` interpose (not used for these signals on modern POSIX)
- `waitpid` interpose (forbidden per prior BLOCKER.md)
- DLOPEN interpose re-modification (the existing gate-2 DSO already
  has it; we re-use without change)
