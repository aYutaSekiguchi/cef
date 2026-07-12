# QNX exit139 investigation — read-only BLOCKER (2026-07-11)

**Status: STOP. No further diagnostic work, no design changes, no new runs.**

## Run1 / Run2 / Run3 summary

| Run | Launch form | Result | Classification |
|-----|-------------|--------|----------------|
| 1 | `env LD_PRELOAD=... /path/to/cefsimple ...` inside sh -c | GPU never launched (CHROME_EXE_PATH=.../env) | INVALID (procedural) |
| 2 | `LD_PRELOAD=... /path/to/cefsimple ...` inside sh -c | GPU never launched (errno -87 ELIBBAD) | INVALID (procedural) |
| 3 | `--env LD_PRELOAD=... --env CHROME_EXE_PATH=... -- '/path/to/cefsimple ...'` | GPU launched, crashed exit_code=139, **0 QNX-SIG** | STOP (no QNX-SIG → cannot symbolise) |

## Established FACT (verified from raw log)

| # | FACT | Evidence (run3) |
|---|------|-----------------|
| 1 | Diag DSO loaded in 9 distinct PIDs | `grep -c TRACER.*LOADED` = 9; PIDs: 602141, 606238, 606240, 614431, 614433, 614434, 700451, 700452, 708639 |
| 2 | Browser (606238) reports `exit_code=139` for crashed GPU child | line 1398: `[606238:1:0711/121331.408630:ERROR:content/browser/gpu/gpu_process_host.cc:1006] GPU process exited unexpectedly: exit_code=139` |
| 3 | GPU child reached `ready to call SubmitFrame` and emitted `DMAbuf missing` error BEFORE the crash | pid 614431 emitted `QnxGpuService::Initialize: ... ready to call SubmitFrame` at 121322.961, `QnxRenderProducer::Initialize: required DMAbuf ... missing` at 121323.x, then crash reported at 121331.408 |
| 4 | **0 [QNX-SIG] lines** from any PID in the entire run3 log | `grep -c '^\[QNX-SIG\]' /tmp/exit139-instrumented/exit139-instrumented-run3.log` = 0 |
| 5 | Diag DSO LOADED + MAPs in pid 614431 (inferred first GPU child via proximity) | line 179: `[TRACER] pid=614431 ... LOADED`; multiple [MAP] snap=1..11 lines from pid=614431 |

## (1) Crash target host_id ↔ PID mapping (proximity inference, soft)

| host_id | PID (inferred) | Evidence |
|---------|----------------|----------|
| 1 | **614431** (soft inference by proximity) | `OnGpuServiceLaunched: host_id=1` at 121318.916; same PID emitted `ready to call SubmitFrame` 121322.961 and `DMAbuf missing` 121323.x; `OnChannelDestroyed: host_id=1` at 121331.527; `exit_code=139` reported at 121331.408 |
| 2 | 708639 (soft inference) | `OnGpuServiceLaunched: host_id=2` at 121331.526; alive at end of run |

PIDs 614433, 614434 — same TRACER group as 614431 (browser-emitted prefix `[606238:1:...]`), likely GPU child threads or helper processes.

**This host_id ↔ PID mapping is NOT source-explicit.** The `OnGpuServiceLaunched` line does not contain the child PID. The mapping is by temporal proximity. If a future source revision emits the child PID, the mapping may need re-verification.

## (2) Existing sigaction registrations for SIGSEGV/SIGBUS/SIGILL (read-only audit)

| File | Line | Purpose | Permanent? | Affects GPU process? |
|------|------|---------|-----------|---------------------|
| `base/debug/stack_trace_qnx.cc` | 467-471 | QNX-specific stack-trace dump on signal | NO (install → dump → restore inside the signal handler itself) | No — installed transiently during dump |
| `base/debug/stack_trace_posix.cc` | 1003-1007 | Same on non-QNX POSIX | NO (transient, same pattern) | No |
| `third_party/angle/src/common/system_utils_posix.cpp` | 462-475 | ANGLE `EnableInProcessStackDumping` | YES (process-wide if called) | **No** — search confirms only `ui/ozone/demo/{skia_demo,ozone_demo}.cc` call this; production GPU process does NOT install it |
| `third_party/crashpad/crashpad/handler/handler_main.cc` | 403 (SIGTERM only) | crashpad_handler Main | NO crashpad_handler on QNX (only noop_client_stubs via `crashpad_qnx.patch`) | N/A |

**No permanent process-wide Chromium-side sigaction handler for SIGSEGV/SIGBUS/SIGILL on the QNX GPU process is found in source.** The diag's handler SHOULD be the only permanent one for these signals in the GPU process. **However, this is a static source audit; runtime evidence of "no override" is not gathered.**

The source audit does NOT prove:
- The diag's sigaction call returned 0
- No later code overrode the diag's handler
- The diag's handler was the active handler at the moment of the crash

These require runtime verification (per option A below), which the read-only constraint forbids.

## (3) Signal vs synthesized 139 — UNDETERMINED

For the GPU child (pid 614431):
- 0 [QNX-SIG] lines emitted from pid 614431 in the entire log
- No signal-related markers from pid 614431
- The browser reports `exit_code=139` at line 1398

**Candidates (none confirmed)** for what 139 means in this run:

| Candidate | Plausibility | Evidence for / against |
|-----------|-------------|-------------------------|
| (a) WIFSIGNALED + WTERMSIG=11 (real SIGSEGV); handler should have fired | LOW | No [QNX-SIG] from pid 614431. But "handler effective" is unverified (see (4)). |
| (b) WIFEXITED + WEXITSTATUS=139 (explicit `_exit(139)` from CHECK / NOTREACHED / abort / LOG(FATAL) path) | UNKNOWN | No source citation of which path. CHECK / NOTREACHED / abort do not by themselves produce 139; the convention depends on the caller. |
| (c) Status normalization by QNX shell or Chromium child wait wrapper | UNKNOWN | No source audit done in this turn. |
| (d) Later sigaction in another constructor or library init overrode the diag's handler | UNKNOWN | Static source audit finds no candidate, but no runtime interpose was added. |
| (e) `StagingBuffer's SharedImage failed` (renderer thread) is the crash cause and pid 614431's exit_code=139 is a coincidence | UNKNOWN | The `SharedImage failed` errors are from pid 606238:17 (browser viz thread), not from pid 614431. Whether they caused the GPU process exit cannot be established from the raw log without a PID/source cross-check. |

**The 139 exit_code alone is NOT a determination of "signal-triggered" vs "non-signal".** It only tells us `WIFEXITED(status) && WEXITSTATUS(status) == 139` OR `WIFSIGNALED(status) && WTERMSIG(status) == 11`. The browser's `gpu_process_host.cc` interprets this as a GPU crash but does not distinguish the two paths in the printed line. The raw `status` field is NOT printed.

The earlier uninstrumented baseline (sessions) also showed `exit_code=139` with no [QNX-SIG]. Whether the crash mechanism is the same in uninstrumented vs instrumented runs is UNDETERMINED — the diag may have changed behavior (e.g. via LD_PRELOAD side effects), or the crash may be the same.

## (4) DSO handler install does not capture sigaction return code

`/tmp/exit139-diag/exit139_diag.cc:348`:
```cpp
::sigaction(kSigs[i], &sa, nullptr);
```
**Return value is DISCARDED.** The source does not record whether the install succeeded. We CANNOT confirm from the source that the handler is active at runtime.

**Open questions (runtime, not source-auditable):**
- Did `sigaction()` return 0 for all three signals?
- Did any later code (in this process, in any dynamically-loaded library) call `sigaction()` again and override the diag's handler?
- Did the diag's handler get uninstalled or replaced before the crash?

These require runtime measurement, which the read-only constraint forbids.

## Established FACT (clear, in this run3)

| # | FACT | Source |
|---|------|--------|
| A | Diag DSO loaded in 9 PIDs (including GPU child pid 614431) | `[TRACER] LOADED` lines + PIDs in log |
| B | GPU child pid 614431 reached `ready to call SubmitFrame` and emitted `DMAbuf missing` before crash | pid 614431-emitted lines 121322.961, 121323.x |
| C | Browser reports `exit_code=139` for the GPU process | line 1398: `GPU process exited unexpectedly: exit_code=139` |
| D | 0 [QNX-SIG] lines from any PID in this run | grep on log |
| E | The diag's `sigaction()` return codes are not captured in the source | source review |

## Candidates eliminated (because not proven)

- "GPU process died from signal X" — not proven
- "GPU process exited via explicit `_exit(139)`" — not proven
- "StagingBuffer SharedImage failure caused the GPU crash" — not proven
- "The diag's handler is the active SIGSEGV handler" — not proven
- "139 disappeared (or appeared) because of LD_PRELOAD" — not tested (only 1 instrumented run)

## Safe next-step candidates (still not approved, design-change-free preference)

| # | Approach | Source change? | Runtime change? | What it would clarify |
|---|----------|----------------|-----------------|---------------------|
| **A** | Add sigaction return code and immediate `sigaction(sig, NULL, &old)` readback of the active handler in the diag constructor (and in `dlopen` interpose). Then interpose `sigaction` (and `signal` for legacy) to record the WHO / WHEN / WHAT of every subsequent sigaction install in the process. Output via the existing `[QNX-SIG] ...` / `[MAP] ...` line format. | YES (add to `exit139_diag.cc`; new interpose symbols: `sigaction`, `signal`) | YES (one QEMU run with the new build) | Whether the diag's handler is the active one at crash time; who (if anyone) overrode it; whether sigaction failed |
| **B** | Add raw wait-status fields to the browser's GPU-crash report path. The `gpu_process_host.cc:1006` line currently prints `exit_code=NNN` only. A diag (separate from the GPU-side diag) running in the browser could interpose `waitpid` / `wait4` and log `WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG / status` raw fields. Output via `[QNX-SIG] ... wait-status ...` lines. | YES (add browser-side diag DSO) | YES (one QEMU run) | Whether the GPU child was signal-terminated (WIFSIGNALED) or normal-exit (WIFEXITED) — definitive for question (3) |

**Both require a new diag DSO (separate from `exit139_diag.so`) and a new QEMU run.** They are read-friendly diagnostics (no source-tree edits, no chromium-tree edits), but they ARE design changes (new DSO source, new build). They are EXPLICITLY FORBIDDEN by the current read-only constraint.

**Less-invasive option C**: at minimum, add a raw `WIFEXITED / WEXITSTATUS / WIFSIGNALED / WTERMSIG` print on the existing `gpu_process_host.cc:1006` line. This is a ONE-LINE source-tree change to the CEF build. Still requires a clean re-bootstrap and a new QEMU run. Also FORBIDDEN by current read-only constraint.

## Stop conditions met (this turn)

Per the supervisor's stop conditions, ANY of these is a stop:
- 0 QNX-SIG → **YES, this is the stop trigger** (QNX-SIG is required for symbolisation, none observed)
- 139 disappeared → ✗ (139 still present, consistent with baseline)
- MAP not correlatable → ✗ (MAPs exist, but for the GPU process and they do not include a fault moment)
- terminate/134 recurred → ✗
- new gate failure → ✗

Single stop trigger: **0 QNX-SIG**.

## Repo state

- main worktree: clean (only untracked from prior investigations)
- HEAD: `debf52a63`
- CEF repo tracked files: unchanged
- BLOCKER.md is untracked (per project policy, investigation docs are untracked)
- sudo not used, args.gn not hand-edited, no commit

## Safe next-step candidates (full description, NOT approved)

### Option A: DSO-side sigaction return + readback + interpose of subsequent sigaction

Source changes (in `/tmp/exit139-diag/exit139_diag.cc`, outside the CEF repo and outside the chromium tree):

1. In `init()`, after each `::sigaction(kSigs[i], &sa, nullptr)`, capture the return code and emit `[QNX-SIG] sig=N sigaction-rc=N` (async-signal-safe format via `lb_ws_literal` + `lb_wd`).
2. Immediately after each successful install, do `::sigaction(kSigs[i], NULL, &old)` and emit `[QNX-SIG] sig=N active-handler=...` (a small bounded-hex format for the handler address; avoid `dladdr` in this path).
3. Add interpose for `sigaction` and `signal` (legacy). On each call, record the WHO (caller return address), the WHICH (signal), the WHAT (handler/oldaction). Store in a fixed-size ring or log directly. If the new handler does NOT match the diag's handler, emit `[QNX-SIG] OVERRIDE-sig=N old=0x... new=0x...`.

This would definitively establish:
- Did the diag's sigaction succeed?
- Was the diag's handler still active at the time of any subsequent sigaction override?
- Who overrode it, and when?

**Risk**: the interpose on `sigaction` itself adds a layer to the loader's signal-handler-install path. Must be verified to not recurse.

### Option B: Browser-side waitpid interpose to log raw wait-status fields

Source changes (a new diag DSO for the browser process, in `/tmp`):

1. Interpose `waitpid` / `wait4` / `waitid`.
2. After the real call, extract `WIFEXITED(status)`, `WEXITSTATUS(status)`, `WIFSIGNALED(status)`, `WTERMSIG(status)`, raw `status` value.
3. Emit `[QNX-SIG] wait-status pid=N status=0x... WIFEXITED=N WEXITSTATUS=N WIFSIGNALED=N WTERMSIG=N` via the existing bounded-hex/decimal format.

This would definitively establish:
- Was the GPU child a `WIFEXITED` or `WIFSIGNALED` process?
- If `WIFSIGNALED`, which signal?
- If `WIFEXITED`, what was the exit code (which we already know is 139)?

**Risk**: the browser process is large; adding an LD_PRELOAD to it could affect startup timing. Need to test with the same minimal DSO pattern.

## Both options require a new QEMU run, which is currently forbidden.

## Conclusion

Investigation stops at run3 with 0 QNX-SIG. The crash mechanism (signal vs explicit exit) is UNDETERMINED. The diag's signal handler effectiveness is unconfirmed. Two safe next-step candidates (A and B) are documented above for supervisor's future decision.

No source-tree, chromium-tree, or CEF-repo changes were made. No commits. All artifacts are in `/tmp` or under `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/`.

停止。
