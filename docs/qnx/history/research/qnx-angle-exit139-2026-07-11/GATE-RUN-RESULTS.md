# QNX exit139 gate and instrumented smoke results (2026-07-11)

## Gate v2 (corrected late-dlopen) — PASS

* Raw: `/tmp/exit139-gates/late-dlopen-gate-v2.log`
* Form: `env LD_PRELOAD=/mnt/nfs/out/qnx_release/exit139_diag.so /mnt/nfs/out/qnx_release/exit139_late_dlopen_gate_v2 /mnt/nfs/out/qnx_release/exit139_test_dso.so`
* Key pass criteria (all met):
  - [TRACER] LOADED: 1 (constructor fired)
  - constructor `snap=1` does NOT contain `exit139_test_dso.so` (test DSO not yet loaded)
  - `[late-dlopen] dlopen returned 0x1b18bda3b8` (non-null)
  - `[test-dso] pid=... constructor ran` (test DSO loaded successfully)
  - post-dlopen `snap=2` contains `exit139_test_dso.so` at `base=0x35789c7000`
  - 0 MAP-FAIL
  - exit 0
  - 0 hang
* snap values: snap=1 (21 lines, constructor) and snap=2 (24 lines, post-dlopen re-snapshot)
* test DSO PT_LOAD verified via readelf on /tmp/exit139-diag/exit139_test_dso.so (2 segments: R E and RW)

## Run1 INVALID — procedural invocation

* Form: `env LD_PRELOAD=... /mnt/nfs/out/qnx_release/cefsimple ...` inside sh -c
* qnx_run.sh's `first_token` extraction took `LD_PRELOAD=...` as argv[0]
* `CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/env` set, then `posix_spawnp(.../env): -2 No such file or directory`
* GPU child never launched; 0 ready/Bind/DMAbuf/139; 0 QNX-SIG
* classification: procedural invocation failure, not a DSO perturbation

## Run2 INVALID — POSIX assignment prefix in sh -c

* Form: `LD_PRELOAD=... /mnt/nfs/out/qnx_release/cefsimple ...` (no `env` wrapper)
* qnx_run.sh's `first_token` extracted `LD_PRELOAD=...` literally
* `CHROME_EXE_PATH=/mnt/nfs/out/qnx_release/exit139_diag.so` set
* `posix_spawnp(.../env: -87 Attempting to exec a shared lib)` for every GPU spawn
* GPU child never launched; 0 ready/Bind/DMAbuf/139
* classification: same first_token problem

## Run3 — launch form fixed (--env + guest `./cefsimple`)

* Form: `./tools/qnx_run.sh --virgl ... --env LD_PRELOAD=... --env CHROME_EXE_PATH=... -- '/mnt/.../cefsimple ...'`
* qnx_run.sh sets extra env from `--env` (lines 224-225 in tool source)
* first_token is the actual `cefsimple` path; CHROME_EXE_PATH set to that
* GPU process launches normally
* Per-pid event counts (full log: 2080 lines):
  - [TRACER] LOADED: 9 (browser + GPU + helpers, including pid 614431 = first GPU)
  - [MAP] lines: 1764
  - GPU attempts that reach `ready to call SubmitFrame`: 1
  - `BindGpuControlAndAttachExistingWidgets` (Bind ACK): 1
  - `QnxRenderProducer::Initialize: required DMAbuf ... missing`: 1
  - `exit_code=139`: 1
  - `std::terminate invoked`: 0
  - `exit_code=134`: 0
  - `Reinitialized the GPU process after a crash`: 0
* Per-pid TRACER/MAP: 9 unique pids (602141, 606238, 606240, 614431, 614433, 614434, 700451, 700452, 708639)
* GPU child pid (first attempt) = 614431
  - emit: terminate handler installed (terminate_capture_qnx)
  - emit: TRACER LOADED (diag loaded)
  - emit: MAP snap=1 (full module table)
  - emit: "ready to call SubmitFrame" (121322.961)
  - emit: AttachWidget widget=1 generation=1 (121323.292)
  - emit: "QnxRenderProducer::Initialize: required DMAbuf export extensions or function pointers are missing" (121323.x)
  - 0 emit: QNX-SIG
  - crash: exit_code=139 (121331.408)
* **0 QNX-SIG** from pid 614431 → diag's signal handler did NOT fire

## Why QNX-SIG is 0 in run3

* The diag's signal handler is installed correctly (verified: [TRACER] LOADED
  in GPU child, MAP shows diag's PT_LOAD segments)
* The crash produces `exit_code=139` but does NOT go through a signal delivery
  path. The GPU child is exiting via `_exit(139)` or a similar non-signal
  termination. A signal handler only fires on actual signal delivery
  (SIGSEGV, SIGBUS, SIGILL) via the kernel's signal mechanism.
* This is the SAME exit_code=139 as the uninstrumented baseline (commit
  36988aca + debf52a63) — the crash mechanism is unchanged, but the
  diag cannot observe it because the crash is not signal-triggered.
* Hypothesis: the "DMAbuf missing" error leads to a CHECK() / NOTREACHED()
  that calls `_exit(139)` directly. This is consistent with exit_code=139
  without a signal (WIFEXITED case, like gate 1).
* Cannot symbolise: no QNX-SIG record means no PC/SP captured. PC/DSO
  is **unknown**.

## Stop condition met

* Per supervisor instruction: "QNX-SIGが出ない、139が消える、MAP対応不能、
  terminate/134再発、または新しいgate failureなら停止"
* 0 QNX-SIG is a stop condition
* 139 does NOT disappear (still 1), MAP IS correlatable to GPU pid
  (the GPU has MAPs; we just lack QNX-SIG for the fault moment)
* No terminate/134, no gate failure
* But the absence of QNX-SIG precludes the symbolization step. We stop
  and report raw evidence; no further diagnostic work, no commit.

## Artifacts (all in /tmp, no CEF repo edits, no commit)

* `/tmp/exit139-gates/late-dlopen-gate-v2.log` — late-dlopen gate v2
* `/tmp/exit139-instrumented/exit139-instrumented-run1.log` — INVALID (run1)
* `/tmp/exit139-instrumented/exit139-instrumented-run2.log` — INVALID (run2)
* `/tmp/exit139-instrumented/exit139-instrumented-run3.log` — instrumented cefsimple
* `/tmp/exit139-diag/` — diag DSO + mini_gate + inherit_parent + late_dlopen_gate_v2 + test_dso source/build
* `/home/yuta/chromium/src/out/qnx_release/exit139_*` — copied binaries for guest access

## Status

* main worktree: clean (only untracked from prior investigations)
* HEAD: `debf52a63` (no commit this turn)
* CEF repo tracked files: unchanged
* sudo not used, args.gn not hand-edited, no manual patch apply
