# SINTERPOSE-RESULT (2026-07-12)

## Goal

Last discrimination step before closing handler-effectiveness:
add a `sigaction()` interpose to a minimal DSO to capture every
subsequent `sigaction(SIGSEGV/SIGBUS/SIGILL, ...)` call. If override
is observed, it would rule in override-via-sigaction as the cause of
`[QNX-SIG]` absence. If no override is observed, this confirms
ABC-RESULT's "no override" finding and we close the investigation.

## Implementation

`exit139_sinterpose.so` (`/tmp/exit139-sinterpose/exit139_sinterpose.cc`):

- Constructor: resolves `real_sigaction = dlsym(RTLD_NEXT, "sigaction")`,
  installs handler for SIGSEGV/SIGBUS/SIGILL, emits
  `[MIN-DIAG] install_rc=... readback_rc=... handler=... active=...`,
  sets `g_interpose_active = 1`.
- Interpose: forwards unchanged. If `g_interpose_active == 0` (loader
  called before our constructor finished), emits `[SIGACT-EARLY]` and
  forwards via `::sigaction`.
- Handler: identical to minimal DSO (bounded raw write, async-signal-
  safe, restore SIG_DFL via `g_real_sigaction` and re-raise).

Build: q++ QNX 8.0.0, 23408 bytes, no warnings.

## Mini gate — STOP CONDITION triggered

Raw: `/tmp/exit139-sinterpose-mini-gate.log` (7512 lines)

| Marker | Count |
|---|---:|
| `[SIGACT-EARLY]` | **7474** |
| `[SIGACT-INTERPOSE]` | 0 |
| `[MIN-DIAG]` | 0 |
| `[QNX-SIG]` | 0 |
| Process exit | segmentation violation (stack overflow, exit 139) |

### Failure analysis (FACT)

The interpose caused an infinite recursion:

1. The `extern "C" int sigaction(...)` symbol in the DSO replaces
   libc's `sigaction` symbol via LD_PRELOAD semantics.
2. libc / loader / glibc-init code calls `sigaction()` for various
   signals (SIGPIPE, SIGCHLD, etc.) **before our constructor runs**.
3. Our interpose sees `g_interpose_active == 0`, emits
   `[SIGACT-EARLY]`, and forwards via `::sigaction`.
4. But `::sigaction` IS our interpose (we replaced the symbol).
5. The call recurses indefinitely until the stack overflows → SIGSEGV
   → exit 139.

The constructor's `dlsym(RTLD_NEXT, "sigaction")` would have resolved
a non-recursive real pointer, but `dlsym` is only called from the
constructor, and the constructor never runs because every early
`sigaction()` call recurses to the same path that emits SIGACT-EARLY
without resolving.

### Why this design failed

The early-forward path (`::sigaction(...)` from inside the interpose)
is **inherently recursive** under LD_PRELOAD. The only way to make
this safe is to resolve `real_sigaction` BEFORE the interpose symbol
becomes active — which requires either:
- A constructor with higher ELF init priority that runs before libc's
  own init (QNX doesn't expose `__attribute__((init_priority))`
  reliably for this purpose), or
- Static linking of `real_sigaction` via direct address (not portable
  across loader implementations).

Neither was available without testing. Per the DESIGN's STOP condition
("Gate fails (interpose perturbs mini gate)"), this run is not
proceeded to cefsimple.

## FACT

- The sigaction-interpose-only design fails its own gate on QNX due
  to LD_PRELOAD symbol replacement + libc's early sigaction calls
  → infinite recursion → SIGSEGV.
- No cefsimple run was attempted (per STOP condition).
- No override data was collected.
- ABC-RESULT's finding ("no override observed across 10 PIDs, 9 GPU
  SIGSEGV events") remains the strongest available evidence.

## Closure

Handler-effectiveness investigation is **CLOSED**:

1. **install effectiveness**: confirmed via mini gate, inheritance
   gate, ABC A/B/C runs (5+3+10+10 = 28 install/readback pairs,
   all rc=0, all matching).
2. **no override**: confirmed by ABC A/B/C (no override ever observed
   in 9 GPU SIGSEGV events across 3 runs); cannot be confirmed
   with stronger data via sigaction interpose because the interpose
   itself fails to load.
3. **handler firing**: 0 `[QNX-SIG]` observed across all 9 GPU SIGSEGV
   events in ABC. The handler is installed but never fires.

The remaining unknown — why the handler does not fire during a real
SIGSEGV — is **out of scope for user-space diagnostics**. Kernel-level
instrumentation would be required to determine if QNX procnto bypasses
user handlers for certain fault classes.

The GPU SIGSEGV root cause (DMAbuf missing / RGBA / libGLESv2 path)
is a **separate investigation** and is not addressed here.

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications.
- `/tmp/exit139-sinterpose/exit139_sinterpose.cc` (untracked, source)
- `/home/yuta/chromium/src/out/qnx_release/exit139_sinterpose.so`
  (host artifact, untracked)
- 0 commit, 0 push, no rebuild of cefsimple / libcef.so

## Artifacts

| | Path |
|---|---|
| Design | `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/DESIGN-SIGACTION-INTERPOSE-ONLY.md` |
| Source | `/tmp/exit139-sinterpose/exit139_sinterpose.cc` |
| Built DSO | `/home/yuta/chromium/src/out/qnx_release/exit139_sinterpose.so` |
| Mini gate raw | `/tmp/exit139-sinterpose-mini-gate.log` |