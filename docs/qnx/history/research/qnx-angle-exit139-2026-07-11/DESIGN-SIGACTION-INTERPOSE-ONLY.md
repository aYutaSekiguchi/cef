# DESIGN-SIGACTION-INTERPOSE-ONLY (2026-07-12)

## Goal

In ABC-RESULT, handler install effectiveness is confirmed (10 PIDs,
all rc=0, all readback matching, no override ever observed). Handler
firing is NOT observed (0 `[QNX-SIG]` across all 9 GPU SIGSEGV events
in A/B/C). The candidate cause (kernel bypass) is **not testable from
user space** per ABC-RESULT.

This design is the **last discrimination step** before closing
handler-effectiveness: add a `sigaction()` interpose to a minimal DSO
to capture every subsequent `sigaction(SIGSEGV/SIGBUS/SIGILL, ...)`
call from any code path in any process. If the interpose records a
late install with `new_handler != our handler`, that rules out
override-via-sigaction as the cause and supports the "handler is
still in place but never fires" hypothesis. If no override is ever
recorded, the data point is the same as ABC-RESULT (no override), but
the interpose itself is a stronger guarantee because it captures
ALL calls (including ones that fail to call our handler, e.g., direct
syscall, kernel-set).

This design is **also the last attempt** before the user-directed
closure. If the interpose run produces the same `[QNX-SIG]=0` and
the same baseline GPU SIGSEGV pattern as ABC-RESULT, we close
handler-effectiveness with: handler installed + never overridden
+ never fires = the diagnostic cannot be performed from user space
on this QNX runtime; the GPU SIGSEGV root cause must be addressed
separately (DMAbuf / RGBA / libGLESv2 path).

## Scope

### In scope

- One new DSO `exit139_sinterpose.so` containing ONLY:
  1. Constructor: resolve real `sigaction` via `dlsym(RTLD_NEXT, "sigaction")`.
     Install `SIGSEGV/SIGBUS/SIGILL` handler with same safe body as
     minimal DSO. Emit constructor log + install/readback line per
     signal.
  2. Interpose `sigaction()`: recursion guard (`thread_local int`
     is permitted for non-handler code; or static int per process),
     forward unchanged, emit `[SIGACT-INTERPOSE] sig=N rc=N new=... old=...`
     line per call.
  3. Handler body: identical to minimal DSO (no fprintf, no dlsym,
     no MAP, no dlopen interpose).

### Out of scope

- MAP / dlopen interpose (prior interpose DSO had this; complexity
  may perturb startup per HANDLER-EFFECTIVENESS-RESULT.md).
- constructor-time `dlsym(RTLD_NEXT, ...)` for any symbol other than
  `sigaction` itself.
- Per-PID tagging in handler (handler cannot allocate or call non-async-safe).
- Any CEF/Chromium tracked source change.

## Implementation rules

### Constructor (1 per process)

1. `real_sigaction = (sigaction_fn)dlsym(RTLD_NEXT, "sigaction")` —
   safe because dlsym in constructor is normal context, not in handler.
2. For each of SIGSEGV/SIGBUS/SIGILL:
   - `sigaction(sig, &sa, NULL)`; capture rc; emit
     `[MIN-DIAG] sig=... install_rc=N handler=0x... readback=0x...`
   - `sigaction(sig, NULL, &old)`; emit
     `[MIN-DIAG-READBACK] sig=... rc=N active=0x...`
3. Mark `g_interpose_active = 1` so the interpose knows the real
   pointer is resolved.

### Interpose `sigaction()`

- Signature: `extern "C" int sigaction(int sig, const struct sigaction* act, struct sigaction* oldact)`.
- Body:
  - If `g_interpose_active == 0` (loader called sigaction before our
    constructor finished): forward to `::sigaction` (the libc
    entry point, NOT our wrapper — but we cannot call our wrapper
    without infinite recursion; the loader's call will go through
    the PLT to libc, which is the real sigaction). Emit a
    `[SIGACT-EARLY] sig=N` log line and return `::sigaction(...)`.
  - If `tls_depth > 0`: forward to `real_sigaction` (we got called
    from inside our own logging code). No log.
  - Else: increment `tls_depth`, call `real_sigaction(sig, act, oldact)`,
    decrement `tls_depth`, emit
    `[SIGACT-INTERPOSE] sig=N rc=N new=0x... caller_ra=0x...`,
    return rc.

### Handler

- Same as minimal DSO:
  - emit `[QNX-SIG] pid=... sig=... si_code=... si_addr=... PC=... SP=...`
    using bounded raw write (no printf).
  - restore SIG_DFL via `real_sigaction(sig, &dfl, NULL)` (NOT the
    interpose wrapper), then `kill(getpid(), sig)`, then `_exit(128+sig)`.

### Logging rules

- All normal-context logs (constructor, interpose) use the same
  bounded writer as minimal DSO.
- Handler logs use only `write(2, ...)`.
- NO use of `fprintf`, `std::cout`, `std::cerr` anywhere.

### Recursion guards

- `static int g_interpose_active = 0` (set at end of constructor).
- `static __thread int tls_depth = 0` (incremented / decremented
  around `real_sigaction` calls in the interpose).

### Async-signal-safety in handler

- Handler reads `getpid`, `kill`, `sigemptyset`, `_exit` only.
- Handler re-raise path uses `real_sigaction` (the resolved function
  pointer, not the interpose wrapper symbol).

## Gates (must pass before cefsimple run)

### Mini gate (`LD_PRELOAD=exit139_sinterpose.so ./exit139_mini_gate`)

- LOADED marker (added to interpose DSO, optional but useful)
- sigaction install rc=0 for SIGSEGV/SIGBUS/SIGILL
- readback matches install
- No recursion (no MAP-FAIL equivalent, but the loader's initial
  sigaction calls appear with `[SIGACT-EARLY]` and rc=0)
- `[QNX-SIG] pid=... sig=11` from deliberate null deref

### Inheritance gate

- Parent LOADED, child LOADED
- Child sigaction install rc=0, readback matches
- `[QNX-SIG]` from deliberate fault in child

## Run (after gates pass)

`qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 --env LD_PRELOAD=/mnt/nfs/out/qnx_release/exit139_sinterpose.so --env CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/cefsimple -- /mnt/nfs/out/qnx_release/cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr`

Raw: `/tmp/exit139-sinterpose-cefsimple-run1.log`

## STOP conditions

- Gate fails (interpose perturbs mini gate).
- `screen_create_context failed` / `GLDisplayEGL::Initialize failed`
  (DSO perturbs runtime).
- `exit_code != 139` (no GPU SIGSEGV reproduced).
- `ready=0` / `SubmitFrame=0` / `DMAbuf=0` (no GPU producer reached
  the critical path).

## Success criteria

- `QNX-SIG > 0` from GPU child PIDs that crash: handler fire was
  blocked in minimal DSO because the GPU child had no
  user-space handler installed; interpose shows a late
  sigaction() override that minimal missed → user can address.
- `QNX-SIG = 0` and no override ever recorded: confirms ABC-RESULT
  finding; close handler-effectiveness investigation.

## Closure

If success criteria's second bullet holds, this investigation is
CLOSED. The GPU SIGSEGV root cause (DMAbuf / RGBA / libGLESv2
path) is out of scope for handler-effectiveness and must be
addressed in a separate Phase.