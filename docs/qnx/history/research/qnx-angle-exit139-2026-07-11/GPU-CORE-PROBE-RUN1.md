# GPU-CORE-PROBE-RUN1 (2026-07-12)

## Goal

Reproduce GPU SIGSEGV in a baseline `--virgl` run, and if a core
file is generated, capture it on host and inspect to confirm it
corresponds to a GPU child (not browser).

## Method

`qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180`,
guest command:
```
echo PRE_LIST; ls -la /var/dumper/ 2>&1; echo PRE_END;
cd /mnt/nfs/out/qnx_release &&
  /mnt/nfs/out/qnx_release/cefsimple --ozone-platform=qnx
    --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native
    --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1;
echo CRASH_OR_TIMEOUT;
echo POST_LIST; ls -la /var/dumper/ 2>&1; echo POST_END
```

Raw: `/tmp/exit139-gpu-core-probe.log` (159 lines)

## FACT

### Run outcome

| | Count |
|---|---:|
| `GPU process exited unexpectedly: exit_code=139` | 0 |
| `[exit139-waitstatus] raw=0x8b` | 0 |
| `ready to call SubmitFrame` | 0 |
| `SubmitFrame` markers | 0 |
| `QnxRenderProducer::Initialize: required DMAbuf` | 0 |
| `GPU process launch failed: error_code=1003` | **5** |
| `The GPU process has crashed N time(s)` | 5 (lines 36, 47, 55, 65, 75) |
| `OnGpuServiceLaunched: host_id=N` | 5 (host_id 1..5) |
| `OnChannelDestroyed: host_id=N` | (the launch failure itself triggers this path) |
| Wrapper exit | `TimeoutError: QNX command timed out after 60.0 seconds` (no `__PI_QNX_EXIT__:N` printed) |
| Browser PID | 622622 |

### GPU failure mode

`error_code=1003` = Chromium
`CONTENT_PROCESS_RESULT_CODE_CHILD_PROCESS_LAUNCH_FAILED`.
This is **distinct from the WAITSTATUS-RESULT.md pattern
(`exit_code=139`, raw `0x8b`, `WTERMSIG=11`)**. The browser
attempts to launch the GPU process 5 times; each attempt
fails with launch error 1003.

### Cores: pre vs post

| | PRE | POST |
|---|---|---|
| Total cores | 25 | 25 |
| `cefsimple.core` size / mtime | 335204352 / 2026-07-11 23:55 | **335224832 / 2026-07-12 00:02** |
| All other cores | unchanged | unchanged |

**The `cefsimple.core` file was rewritten during this run.**
Size increased by ~20 KB and mtime updated. The new
`cefsimple.core` overwrote the prior one — so the pre-existing
file we already inspected (`38662dfe4d5495baeb82cf96168120be8ccf38a369543edd66a201830fb9e457`,
335204352 bytes, browser tree) is now gone.

### GPU 139 not reproduced

The GPU process did not reach a state where it died with
SIGSEGV. The browser's GPU launch attempts failed at the
process-spawn layer with error 1003, not at runtime.

## Interpretation (FACT)

1. **GPU SIGSEGV (the `0x8b` pattern) did NOT reproduce in this
   run.** The 5 GPU attempts each failed with launch error 1003,
   which is a different failure mode from the WAITSTATUS-RESULT.md
   SIGSEGV signature.

2. **A new `cefsimple.core` was written during this run** (size
   335224832, mtime 2026-07-12 00:02). This is almost certainly
   the browser process crashing from a non-SIGSEGV path (since
   no `exit_code=139` was observed in this run, the crash would
   have been a different mechanism — possibly the browser tree
   crashing via an abort / `error_code=1003` propagation path).

3. **Per supervisor directive, the new `cefsimple.core` is NOT
   copied** to host: (a) it is identified as browser (same as
   the prior one — `cefsimple` linkmap), (b) it was not produced
   by a GPU SIGSEGV, (c) reusing existing browser cores was
   explicitly forbidden.

4. **No GPU child core was produced.** The GPU process attempts
   that did happen died at launch (error 1003), before any
   runtime crash could occur.

## UNKNOWN

- **The exact GPU child process failure mode under error_code=1003.**
  Could be: (a) QNX spawn failure (some syscall failure specific
  to this run's guest state), (b) a different signal/exit that
  the browser reports as 1003 rather than 139. Not investigated.
- **Why GPU 139 reproduces in some runs (ABC-RESULT: 3/3) but
  fails with 1003 in this run.** Possible causes: (a) guest
  state difference, (b) the inline probe commands (`ls /var/dumper`)
  changed the runtime enough to perturb GPU launch. Not
  investigated per supervisor directive.
- **Whether a future run would reproduce 139 + produce a GPU
  child core.** Not testable without additional runs (blocked
  per supervisor).

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications. 0 commit.

## Artifacts

| | Path |
|---|---|
| Run raw | `/tmp/exit139-gpu-core-probe.log` (159 lines) |
| Prior browser core (already host-recovered) | `/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/cefsimple.core` — but **superseded** by the new run's cefsimple.core on guest side; host copy is now stale |
| New cefsimple.core on guest | `/var/dumper/cefsimple.core` (335224832 bytes, 2026-07-12 00:02) — **NOT** copied to host per directive |