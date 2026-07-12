# HANDLER-EFFECTIVENESS-RESULT (2026-07-12)

## Goal

Determine whether the diag DSO's signal handler did not emit
`[QNX-SIG]` in the prior instrumented cefsimple runs because of
install failure, override, or non-firing handler, given that
WAITSTATUS-RESULT.md established the GPU child crash as a real
SIGSEGV (WIFSIGNALED=1, WTERMSIG=11, WCOREDUMP=1).

## Method

`exit139_diag.so` was extended (Phase A) with:

1. Constructor captures the rc of every `sigaction(sig, &sa, NULL)`
   call for SIGSEGV / SIGBUS / SIGILL and reads back the active
   handler via `sigaction(sig, NULL, &old)` to compare against ours.

2. Interpose on `sigaction()` records every subsequent install
   (signal, rc, new_handler, caller_ra), forwards unchanged.
   `s_real_sigaction` is resolved via `dlsym(RTLD_NEXT, "sigaction")`
   in the constructor before the interpose is active, avoiding
   loader recursion. A recursion guard (`thread_local int
   sigaction_depth`) protects against re-entry from the loader.

3. Handler body unchanged: still async-signal-safe, still emits
   `[QNX-SIG] pid=... tid=unknown sig=N ... PC=... SP=...`.

4. Handler sigaction restoration (re-raise with SIG_DFL before
   re-kill) uses `s_real_sigaction` directly, NOT the interpose
   wrapper, to avoid recursion in the handler.

The DSO was rebuilt; `nm`/`objdump` audit confirmed the handler's
call graph contains only `sigemptyset`, `getpid`, `kill`, `_exit`
at PLT (all async-signal-safe) plus internal `lb_*` writers; no
`fprintf`, `dlsym`, `dl_iterate_phdr`, `::dlopen`,
`emit_sigaction_log` references. `s_real_sigaction` is read via
direct RIP-relative load (not PLT) inside the handler.

## Gates (Phase B)

### Mini gate (`LD_PRELOAD=exit139_diag.so ./exit139_mini_gate`)

Raw: `/tmp/exit139-mini-gate-run1.log`

| | Result |
|---|---|
| LOADED marker | yes (`pid=409623`) |
| sigaction install rc=0 for sig=11 / 10 / 4 | yes |
| Readback matches install handler `0x...c34` | yes |
| MAP-FAIL | none |
| `[QNX-SIG] pid=409623 ... sig=11 PC=0x...835` | yes — handler FIRED |
| WIFSIGNALED=1 WTERMSIG=11 exit_code=139 | yes (process exit 139) |

**Mini gate PASS.**

### Inheritance gate (parent → child via posix_spawn, env inherited)

Raw: `/tmp/exit139-inherit-gate-run1.log`

| | Result |
|---|---|
| Parent LOADED | yes (pid=409623) |
| Child LOADED | yes (pid=450587) |
| Child sigaction install rc=0 / readback matches | yes |
| `[QNX-SIG] pid=450587 ... sig=11` | yes — handler FIRED in child |
| WIFSIGNALED=1 WTERMSIG=11 exit_code=139 | yes |

**Inheritance gate PASS.**

## cefsimple run1 (Phase C)

Launch form (per prior turn run3, formal `qnx_run.sh` path):

```
qnx_run.sh --kill-existing \
  --env LD_PRELOAD=/mnt/nfs/out/qnx_release/exit139_diag.so \
  --env CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/cefsimple \
  -- /mnt/nfs/out/qnx_release/cefsimple --no-sandbox --disable-gpu-sandbox
```

Raw: `/tmp/exit139-handler-effectiveness-run1.log` (710 lines)

### Aggregate counts (FACT)

| Marker | Count |
|---|---|
| `[QNX-SIG]` markers | **0** |
| `exit_code=139` (browser-reported GPU crash marker) | **0** |
| `ready` markers | 1 |
| `SubmitFrame` markers | **0** |
| `GpuProcessHost` / `GPU process exited` (GPU crash marker) | **0** |
| `[ERROR] ui/ozone/common/gl_ozone_egl.cc:26] GLDisplayEGL::Initialize failed.` | 2 (pid 503833) |
| sig=11 `op=install` calls (across all PIDs) | 5 |
| sig=11 `op=readback` calls | 5 |
| sig=11 installs where new_handler ≠ `0x...c34` (overrides) | **0** |
| `MAP-FAIL` markers | 0 |
| `[QNX-ANGLE-TRACE] terminate handler installed` | 4 |
| Final sh exit | `__PI_QNX_EXIT__:139` (segmentation violation, core dumped) |

### Run validity assessment (FACT)

This run is **NOT a clean baseline comparison** for handler
effectiveness:

- `ready=1` but `SubmitFrame=0` — the GPU producer never reached
  the SubmitFrame milestone, which prior turns (WAITSTATUS-RESULT.md,
  BLOCKER.md) established as a precondition for observing the
  GPU SIGSEGV signature (`exit_code=139` + `0x8b`).
- `GLDisplayEGL::Initialize failed.` (×2) — the in-process EGL
  init failed before the GPU producer path.
- The browser tree exited via `segmentation violation (core dumped)`
  in the sh wrapper, but no `exit_code=139` browser-side marker
  fired for any GPU process attempt. The crash is not the
  GPU-process crash pattern from WAITSTATUS-RESULT.md.
- The sigaction interpose itself may perturb startup. **This run
  cannot be used to compare baseline vs. instrumented crash
  signatures** because no clean baseline crash was reproduced.

**Conclusion**: this run is INVALID / perturbed for handler
effectiveness discrimination. The 0 `[QNX-SIG]` cannot be
attributed to handler install / override / non-firing because no
GPU SIGSEGV was actually triggered under the interpose; the run
took a different code path (browser tree crash without GPU child
launch) than the WAITSTATUS-RESULT.md baseline.

## FACT (from gates + run1)

| | FACT | Source |
|---|---|---|
| handler install effectiveness | mini gate install rc=0, readback matches | gate PASS |
| inheritance across fork/exec | child LOADED, install rc=0, readback matches, handler FIRED | gate PASS |
| handler override (mini gate) | none observed | gate |
| handler override (cefsimple run1) | none observed in 5 sig=11 install/readback pairs across 5 PIDs; all new_handler = `0x...c34` (our handler) | run1 log |
| MAP-FAIL | 0 in all runs | gates + run1 |
| `[QNX-SIG]` in run1 | 0 | run1 log |
| GPU SIGSEGV in run1 | not triggered (no SubmitFrame, no exit_code=139) | run1 log |
| ANGLE terminate handler install | 4 markers present in run1 | run1 log |

## Unknown (explicit)

These questions CANNOT be answered from this run and the
supervisor has directed NOT to proceed with additional runs,
interpose enhancement, kernel hypothesis experiments, or
symbolization. They remain OPEN.

- **Was the diag SIGSEGV handler active at the moment of a real
  GPU SIGSEGV?** — recorded only via install/readback in this
  investigation; not via runtime handler fire during a real GPU
  crash (no GPU SIGSEGV was triggered under interpose).
- **Does the QNX kernel bypass user-space handlers for some fault
  classes (procnto direct core-dump path)?** — not observable
  without kernel instrumentation, which is out of scope.
- **Is `write(2, ...)` to fd 2 lost in the post-fault path?**
  not observable without a non-handler-firing comparison.
- **Is the handler lost to a race with another dynamic
  initializer in the GPU process path?** — not tested (no GPU
  process launch in run1).

## Closure statement

- **Handler-effectiveness investigation is CLOSED** for this
  session with the conclusion that:
  - The handler is installed correctly (mini gate, inheritance
    gate, run1 install/readback all confirm rc=0 and matching
    handler).
  - The handler is never overridden (run1 saw 0 non-`0x...c34`
    installs for sig=11 across all 5 PIDs).
  - run1 did NOT reproduce the GPU SIGSEGV baseline, so the
    absence of `[QNX-SIG]` cannot be attributed to handler
    non-firing from this run alone.

- **No additional run, interpose change, kernel experiment, or
  symbolization will be performed** per supervisor directive.

## Implications for Phase 7 / remaining work

- **Recursive GlobalMutex fix (`debf52a63`) remains in effect**:
  eliminates `std::terminate` paths. Confirmed: no
  `[QNX-ANGLE-TRACE]` "captured" lines (only "installed"
  markers), no `std::terminate` in this run.
- **Remaining 139 crash is a real SIGSEGV** but its kernel
  bypass / user-handler path on QNX remains uninvestigated.
- **Default CEF Views mode crash blocker** (`CefBrowserView::
  CreateBrowserView` path) was the actual crash in run1 — same
  blocker noted in prior turn; no new evidence here.

## Artifacts

| | Path |
|---|---|
| DSO source | `/tmp/exit139-diag/exit139_diag.cc` |
| mini_gate source | `/tmp/exit139-diag/exit139_mini_gate.cc` |
| inherit_parent source | `/tmp/exit139-diag/exit139_inherit_parent.cc` |
| DSO + gates (host) | `/home/yuta/chromium/src/out/qnx_release/exit139_diag.so`, `exit139_mini_gate`, `exit139_inherit_parent` |
| DSO + gates hashes | `/tmp/exit139-artifact-hashes.txt` |
| Mini gate raw log | `/tmp/exit139-mini-gate-run1.log` |
| Inheritance gate raw log | `/tmp/exit139-inherit-gate-run1.log` |
| cefsimple run1 raw log | `/tmp/exit139-handler-effectiveness-run1.log` |
| Design doc | `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/DESIGN-HANDLER-EFFECTIVENESS.md` |

## Tracked source state

Unchanged from prior turn. No CEF/Chromium tracked modifications.
`git status` shows no diff in `cef/patch/`, no diff in chromium
sources. All investigation artifacts in `/tmp` and the doc in
`docs/qnx/history/research/qnx-angle-exit139-2026-07-11/`
(untracked, per investigation-doc policy).