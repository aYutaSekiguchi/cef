# QNX child termination raw wait-status investigation DESIGN (2026-07-11)

## Goal

Determine whether the GPU process's `exit_code=139` reported by
`GpuProcessHost::OnProcessCrashed` is:
- WIFSIGNALED + WTERMSIG=11 (real SIGSEGV) — the diag's signal handler
  should have fired; absence of [QNX-SIG] is a handler-effectiveness
  problem, OR
- WIFEXITED + WEXITSTATUS=139 (explicit `_exit(139)`) — the diag's
  signal handler is not in scope; the crash is a non-signal path.

## Data path (read-only source audit, FACT)

| Step | Qualified file/function | What happens to status |
|------|--------------------------|------------------------|
| 1 | `base/process/kill_posix.cc:42` (`GetTerminationStatusImpl`) | `*exit_code = status;` — **the raw `int` returned by `waitpid(2)` is stored verbatim in `*exit_code`**. The function then checks WIFSIGNALED/WIFEXITED to compute a `TerminationStatus` enum, but the raw bits are preserved. |
| 2 | `content/browser/child_process_launcher_helper_qnx.cc:78-90` (`ChildProcessLauncherHelper::GetTerminationInfo`) | calls `base::GetTerminationStatus(handle, &info.exit_code)`. The QNX-specific helper is in this file. The returned `info.exit_code` is the raw status integer from step 1. |
| 3 | `ChildProcessTerminationInfo::exit_code` (struct field) | the same raw status integer propagates to the browser. |
| 4 | `content/browser/gpu/gpu_process_host.cc:1004-1006` (`GpuProcessHost::OnProcessCrashed(int exit_code)`) | logs `LOG(ERROR) << "GPU process exited unexpectedly: exit_code=" << exit_code;` — **prints the raw `int` value**. No clamping here. (The clamping at line 808 is a different code path used for histogram / CrashExitCodeToString, NOT for the log line we're inspecting.) |

**Therefore the value printed at line 1006 IS the raw `waitpid(2)` `status` integer.** Interpretation:
- WIFSIGNALED(status) ⇒ `(status & 0x7f) == 0` and `WTERMSIG(status) = (status >> 8) & 0xff`
- WIFEXITED(status) ⇒ `WEXITSTATUS(status) = (status >> 8) & 0xff`
- For SIGSEGV (signal 11), the kernel produces `status = 11` (low 7 bits = signal 11, WIFSIGNALED = true, WTERMSIG = 11)
- For explicit `_exit(139)`, the kernel produces `status = (139 << 8) = 0x8b00` (low 7 bits = 0 = WIFEXITED, WEXITSTATUS = 139)

**Distinguishable in the raw integer:**
- `139` (decimal) printed and `0x8b` (the high byte of 0x8b00) → WIFEXITED, exit code 139
- `139` (decimal) printed and `0x8b` (low byte) → **NOT a valid wait status, low byte of a valid signal status is always 0–31 (or 128+).** But wait: gpu_process_host.cc clamps 0..100 at line 808. The 139 printed may be a DECODED value, not the raw status.

Let me re-check the data path — is `info.exit_code` the raw status or the WIF-decoded value?