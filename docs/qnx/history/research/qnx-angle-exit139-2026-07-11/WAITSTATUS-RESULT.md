# WAITSTATUS-RESULT (2026-07-11)

## Established FACT (from 2 uninstrumented runs)

| | run1 | run2 |
|---|---|---|
| Raw log | `/tmp/exit139-waitstatus-runs/exit139-waitstatus-run1.log` | `/tmp/exit139-waitstatus-runs/exit139-waitstatus-run2.log` |
| Browser PID (from `?` PID prefix on browser-emitted lines) | 606237 | 606238 |
| GPU process attempts (from `[exit139-waitstatus] handle=...`) | 614431 (host_id=1), 708639 (host_id=2) | 614431, 708639 (same PIDs) |
| Helper processes | 614435, 614436 | 614435, 614436 (same) |
| GPU crash raw status values | `0x8b`, `0x8b` (two crashes) | `0x8b`, `0x8b` |
| Helper normal status | `0x0` (×2) | `0x0` (×2) |
| Total `[exit139-waitstatus]` markers | 4 per run | 4 per run |

## Data path (read-only source audit, FACT)

| Step | Qualified file/function | What `*exit_code` holds |
|------|--------------------------|--------------------------|
| 1 | `base/process/kill_posix.cc:42` (`GetTerminationStatusImpl`) | **raw `int` from `waitpid(2)`**. `*exit_code = status;` stores the raw value before WIF decoding. The function also returns a `TerminationStatus` enum, but the raw bits are preserved in `*exit_code`. |
| 2 | `content/browser/child_process_launcher_helper_qnx.cc:78-90` (`ChildProcessLauncherHelper::GetTerminationInfo`) | calls `base::GetTerminationStatus(handle, &info.exit_code)`. The QNX-specific helper. `info.exit_code` is the raw status integer from step 1. |
| 3 | `ChildProcessTerminationInfo::exit_code` (struct field) | raw status integer propagates unchanged. |
| 4 | `content/browser/gpu/gpu_process_host.cc:1004-1006` (`GpuProcessHost::OnProcessCrashed(int exit_code)`) | logs `LOG(ERROR) << "GPU process exited unexpectedly: exit_code=" << exit_code;` — `exit_code` is the raw status integer from step 3, printed verbatim. |

## Decoding the raw `0x8b` (FACT from this run)

- `0x8b` (decimal 139) has low 7 bits = `0x0b` = 11 (SIGSEGV), bit 0x80 set (core dumped).
- QNX `sys/wait.h` (verified at `/home/yuta/qnx800/target/qnx/usr/include/sys/wait.h:81-84`):
  - `WIFEXITED(0x8b)` = `(0x8b & 0xff) == 0` = `0 != 0` = **false**
  - `WIFSIGNALED(0x8b)` = `(0x8b & 0xff) != 0 && (0x8b & 0xff00) == 0` = **true**
  - `WTERMSIG(0x8b)` = `0x8b & 0x7f` = **11 (SIGSEGV)**
  - `WEXITSTATUS(0x8b)` = `(0x8b >> 8) & 0xff` = **0** (undefined when WIFSIGNALED)
- QNX `sys/wait.h:65,91` confirms `WCOREFLG = 0x0080` and `WCOREDUMP(s) = s & 0x0080`. The raw `0x8b` has bit 0x80 set → **core dumped**.

**The 139 in the log is NOT a conversion from signal 11 by the browser layer; it is the raw wait status `0x8b` printed as a signed decimal integer.** The browser's `exit_code` parameter is the raw status, not a clamped/derived value.

The `clamp(info.exit_code, 0, 100)` at `gpu_process_host.cc:808` applies to a different code path (histogram / `CrashExitCodeToString`), not to the log line at 1006.

## Crash mode: SIGNAL-DEATH (FACT, not "explicit _exit(139)")

Both GPU crashes in both runs show:
- `raw=0x8b`
- `WIFSIGNALED=1`
- `WTERMSIG=11`
- `WCOREDUMP=1`
- `WEXITSTATUS=-1` (sentinel: not applicable when WIFSIGNALED)
- `info.status=3` (TERMINATION_STATUS_PROCESS_CRASHED per `kill_posix.cc:53-58`)

**The GPU child died from a SIGSEGV (signal 11) with a core dump.** It is NOT an explicit `_exit(139)` path. The 0x8b is the kernel's `waitpid(2)` representation of a SIGSEGV exit.

This means the **earlier uninstrumented-baseline speculation that "139 might be from explicit `_exit(139)`" is INCORRECT**. The earlier `0x8b00` ("`_exit(139)` raw") was a red herring; the actual value is 0x8b. The 139 decimal printed is the raw `0x8b` `int` interpreted as `%d`.

## `known_dead=1` interpretation (CAUTION per supervisor)

`known_dead=1` is observed in the SIGSEGV cases. The `ChildProcessLauncherHelper::GetTerminationInfo` in `content/browser/child_process_launcher_helper_qnx.cc:81` calls `base::GetKnownDeadTerminationStatus` when `known_dead` is true, which in `kill_posix.cc:103-115` first does `kill(handle, SIGKILL)` and then `waitpid` in blocking mode.

**We do NOT conclude that the kill "succeeded"**: `known_dead=1` simply records the supervisor's belief that the child is dead at the time of the call. After the child has already exited, the second `kill(SIGKILL)` call may have failed (ESRCH, process already gone) and the `waitpid` returns the real status. We do not assert "kill succeeded"; we only record the FACT that the QNX path went through the `GetKnownDeadTerminationStatus` branch (not the regular `GetTerminationStatus` branch).

## Reverted change (file untracked; no commit, no tracked diff change)

- Modified: `content/browser/child_process_launcher_helper_qnx.cc` (was untracked; reverted by removing the diagnostic block and the 2 added includes).
- `git status -s` after revert: only the pre-state untracked entry `?? content/browser/child_process_launcher_helper_qnx.cc` (existed before this turn too).
- `git diff content/browser/child_process_launcher_helper_qnx.cc`: empty (no tracked diff change).
- Marker `exit139-waitstatus` in source: 0 (removed).
- 2 runs were completed before revert; raw logs preserved in `/tmp/exit139-waitstatus-runs/`.
- No build artifact rebuild performed after revert (per supervisor: build artifact rebuild not required).

## Conclusion

The GPU child process is dying from a real SIGSEGV (signal 11, core dumped) in both uninstrumented runs. The `0x8b` raw wait status is preserved across the QNX data path (kill_posix.cc → child_process_launcher_helper_qnx.cc → ChildProcessTerminationInfo struct → gpu_process_host.cc log line). The earlier uninstrumented-baseline speculation that 139 might be from explicit `_exit(139)` is now contradicted by direct runtime evidence.

The previous diagnostic investigations (QNX-SIG absence in run3) are explained: the GPU child is in a state where the diag's signal handler is not in scope for the actual fault, OR the diag was not yet installed at the moment of the fault, OR something else overrode it. The new data shows the crash is a real SIGSEGV; the prior failure to capture [QNX-SIG] is a separate question (handler effectiveness / timing / override) and not an indication of non-signal termination.
