# QNX ANGLE path: GPU process exit_code=139 diagnostic DESIGN (2026-07-11)

## Scope

Diagnose GPU child `exit_code=139` (SIGSEGV) that appears in
QNX cefsimple smoke AFTER commit `debf52a63` enabled ANGLE's
recursive `GlobalMutex`.  Cause investigation only.  **No
permanent fix, no commit of any kind.**

Baseline: `debf52a63` (recursive fix in place; MUST NOT regress).
Source log: `docs/qnx/history/research/qnx-angle-throw-tracer-impl-2026-07-11/logs/phaseB-final-cefsimple-smoke.log`

This document is itself an untracked research artifact; not
committed.

## Execution order (REVISED)

1. **Uninstrumented baseline 3 runs** — observe natural
   occurrence rate and timing of `exit_code=139`.
2. **Mini gate + inheritance gate** — verify the
   LD_PRELOAD DSO works in a controlled fault scenario and
   propagates to child processes.  Recorded as
   "instrumented baseline" capability, NOT a baseline
   for the crash analysis.
3. **Instrumented runs (3+) with the diagnostic DSO** —
   each instrumented run is compared to the unistrumented
   baseline.  Differences in reproducibility, timing, or
   event sequence are recorded as **"instrumentation delta"**
   in the RESULT.  We do NOT call instrumented runs
   "baseline".

Rationale: the LD_PRELOAD DSO may perturb timing (extra
`dl_iterate_phdr` calls in constructors / `dlopen` interpose)
or reproducibility (added DSO might catch or mask a
different fault).  The unistrumented baseline is the
authoritative input for "is 139 reproducible at all".

## Current evidence (one unistrumented run, debf52a63 smoke)

### PIDs (FACT, from `[<pid>:1:...]` PID prefixes)

- **Browser**: `434206` (only PID on non-GPU lines).
- **GPU #1** (proxied to host_id=1): PID `528415`.
- **GPU #2** (proxied to host_id=2): PID `536607`.

`OnGpuServiceLaunched: host_id=N starting` does **not** contain
the child PID; we map by proximity to the `terminate handler
installed (pid=X)` line that follows.  Soft inference, not
source-explicit.

Other PIDs in the transcript (`528418`, `528419`, `528420`,
near lines 128-131) have different PIDs from GPU #1
(528415); role is **unknown** (NOT "GPU child threads" — they
are distinct PIDs in the QEMU guest).

### Timeline (QEMU guest clock, raw log line numbers)

| Time | PID | Line | Marker |
|------|-----|------|--------|
| 072201.718820 | 434206 | 36 | `OnGpuServiceLaunched: host_id=1 starting` (GPU #1 spawn begins) |
| 072201.7xx | 528415 | 37-38 | `terminate handler installed` (GPU #1, ×2) |
| 072201.979779-072202.220559 | 528415 | 40-122 | `DisplayEGL::generateConfigs: RGBA(...,0) not handled` (26 prefixed lines) |
| 072202.287814 | 528415 | 127 | `QnxGpuService::Initialize: ... ready to call SubmitFrame` |
| 072202.3xx-9xx | 528418, 528419, 528420 | 128-131 | `terminate handler installed` (role unknown) |
| 072202.871481 | 434206 | 133 | `BindGpuControlAndAttachExistingWidgets: Initialize ack received` (GPU #1 ack consumed) |
| 072202.896493 | 528415 | 139 | `QnxRenderProducer::Initialize: required DMAbuf export extensions ... missing` |
| 072210.279259 | 434206 | 172 | **`GPU process exited unexpectedly: exit_code=139`** |
| 072210.391945 | 434206 | 174 | `OnGpuServiceLaunched: host_id=2 starting` (GPU #2 spawn begins) |
| 072210.392558 | 434206 | 175 | `OnChannelDestroyed: host_id=1` |
| 072210.39xx | 536607 | 176-177 | `terminate handler installed` (GPU #2) |
| 072210.513030-072210.768393 | 536607 | 178-256 | `DisplayEGL::generateConfigs: RGBA(...,0) not handled` (40 prefixed lines) |
| 072210.813681 | 536607 | 258 | `QnxGpuService::Initialize: ... ready to call SubmitFrame` |
| 072210.907739 | 434206 | 259 | `Reinitialized the GPU process after a crash` |
| 072210.909060 | 434206 | 260 | `BindGpuControlAndAttachExistingWidgets: Initialize ack received` (GPU #2 ack consumed) |

### RGBA counts (corrected, no duplicate claim)

| Category | Count | Provenance |
|---|---|---|
| `[528415:...]RGBA` prefixed | **26** | FACT (line 40-122, GPU #1) |
| `[536607:...]RGBA` prefixed | **40** | FACT (line 178-256, GPU #2) |
| `^ERR: DisplayEGL.cpp...RGBA` unprefixed | ~74 | FACT (unprefixed duplicate of `LOG(ERROR)` mirror) |
| Interleave-corrupted (prefix partially overwritten by `[QNX-ANGLE-TRACE]` line — e.g. lines 54/56/58 show `[QNX-A[528415:` followed by `ERR]:`/etc.) | ~20 | FACT that interleave occurs, content reconstruction **NOT** done |
| **Total** | **160** | matches the `grep -c` count |

We do NOT claim the 74 unprefixed lines are *duplicates* of
specific prefixed lines (e.g. we do NOT say "26+40+74=140 and
the rest are duplicates").  What we claim with confidence:
- 26 prefixed lines from GPU #1, 40 prefixed lines from GPU
  #2 — these are the FACT record.
- The 74 unprefixed `^ERR:` lines are the `LOG(severity)`
  mirror of `LOG(ERROR)` writes (a QNX libc behaviour); they
  ARE NOT independent events.
- The 20 "other" lines are interleave artifacts where the
  QEMU serial interleaves the `[QNX-ANGLE-TRACE]` prefix
  with a RGBA line, corrupting the prefix.  We do not
  attempt to attribute each corrupted line to a specific
  PID; we record the interleave phenomenon and continue.

## Crash capture approach

We will NOT use `crashpad` (not deployed on QNX), `gdb` (not
in QEMU guest), or core dump.  Instead: a **standalone
LD_PRELOAD DSO** loaded into the browser process AND
inherited by every child via `posix_spawnp` env propagation
(this propagation is already validated in `36988aca` for
`libangle_throw_tracer.so`).

The DSO is built with the QNX host cross-compiler, placed in
`/tmp/` source tree, copied to `out/qnx_release/`, and
loaded via `env LD_PRELOAD=...` set inside the guest shell
command (NOT via `qnx_run.sh --env`, which we have not
verified to propagate to children).  Concretely:

```bash
./tools/qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 -- \
  'env LD_PRELOAD=/mnt/nfs/out/qnx_release/exit139_diag.so \
   /mnt/nfs/out/qnx_release/cefsimple --ozone-platform=qnx \
     --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native \
     --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr 2>&1 & p=$!; sleep 15; kill -TERM $p 2>/dev/null || true; \
   wait $p 2>/dev/null || true; exit 0'
```

No edits to ANGLE `BUILD.gn`, `libGLESv2.gni`, or any CEF-
managed patch.  No rebuild of `libGLESv2.so` /
`cefsimple`.  The baseline `debf52a63` binary stays
untouched.  The DSO is loaded only when `LD_PRELOAD` is set
in the guest shell.

### DSO components

1. **Constructor** (single-threaded, pre-`main`):
   `dl_iterate_phdr` walk → emit one
   `[MAP] snap=N pid=P tid=T name=... base=0x...` line per
   loaded DSO and one
   `[MAP] snap=N pid=P tid=T seg base=0x... end=0x... filesz=0x...`
   line per PT_LOAD segment.  Snapshot id starts at 1.
   Each line via `write(2, ...)`, per-line C-stack buffer
   < 512 bytes, single `write` per line.
   - **MAP name bounded truncate**: if `info->dlpi_name`
     exceeds 200 bytes after the `name=` field, write
     `[MAP] snap=N pid=P tid=T name=[truncated] base=0x...`
     and set a flag in the next line.  Single `write` per
     line is preserved.
2. **dlopen interpose** (recursion-guarded): after
   `real_dlopen` returns, `dl_iterate_phdr` → new snapshot
   `snap=N+1`.  Recursion guard: `thread_local int
   dlopen_depth` to avoid re-entry from the loader's own
   `dlopen` calls.  If `real_dlopen` is not resolvable via
   `dlsym(RTLD_NEXT, "dlopen")`, emit
   `[MAP-FAIL] reason=dlopen-unresolved` and `abort()` —
   deliberate safe stop in the diagnostic build only.
3. **SIGSEGV / SIGBUS / SIGILL handler** (see
   "Handler invariants" below).
4. **No interaction with `libangle_throw_tracer.so`**:
   that DSO is NOT used in this investigation.

### Handler invariants

`void handler(int sig, siginfo_t* info, void* ucontext_v)`
installed via `sigaction` with `SA_SIGINFO`.  Inside:

1. **Async-signal-safe ONLY**: no `malloc`, `printf`,
   `std::cerr`, `std::string`, `std::ostream`, `snprintf`,
   no locks.  Source-audit-confirmed: handler's call
   graph contains only `write(2, ...)`, custom
   hex/decimal writer, `kill`/`raise`/`_exit`.
2. **Process-global reentrancy guard**:
   `volatile sig_atomic_t in_handler = 0`.  On re-entry
   (signal during signal), set to 1 and `_exit(sig)`.
   No `thread_local` (QNX async-signal-safety is not
   documented).
3. **One-shot write to fd 2**: `char buf[512]` on the C
   stack, single `write(2, ...)` call.  No heap.  No
   recursion.
4. **Numeric formatting**: custom bounded hex/decimal
   writer into `buf`.  No `snprintf` (can take locks
   per POSIX.1-2017; QNX libc does not document it as
   signal-safe).
5. **Minimum record** (we do NOT walk frame-pointer
   chain inside the handler; arbitrary RBP dereference
   can double-fault on non-frame-pointer builds or
   stack canaries):
   - `pid` (POSIX async-signal-safe per
     `signal-safety(7)`),
   - `tid`: if mini gate proves `pthread_self()` safe in
     the handler → record; otherwise `tid=unknown`,
   - `sig` (decimal),
   - `si_code` (decimal),
   - `si_addr` (hex, from `siginfo_t`),
   - `PC` (hex, from `ucontext.gregs[REG_RIP]`),
   - `SP` (hex, from `ucontext.gregs[REG_RSP]`).
   - NO caller RA (would require RBP chain OR DWARF CFI
     unwind; both can double-fault; not in v1).
   - x86_64 has no link register; only RSP and RIP used.
6. **Re-raise default action** (mechanism validated by
   mini gate):
   - **Candidate 1 (preferred)**: in the constructor,
     save prior `struct sigaction` for SIGSEGV / SIGBUS
     / SIGILL via `sigaction(sig, NULL, &prior)`, store
     in a `static sigaction[3]`.  In the handler, after
     writing the record: restore via
     `sigaction(sig, &prior, NULL)`, then
     `kill(getpid(), sig)` or `raise(sig)`.  Expected
     outcome: `exit_code == 128 + sig`.
   - **Candidate 2 (fallback)**: same as 1, but if
     `kill`/`raise` is unavailable or behaves
     unexpectedly on QNX, the handler
     `_exit(128 + sig)` directly.  Same expected
     `exit_code`.
   - **NOT allowed**: directly calling the prior
     `sa_sigaction` from inside the handler.
   - The mini gate validates which candidate produces
     `exit_code == 128 + sig` with no double-fault and
     no log message lost.  One pattern is selected;
     the other is not tried in production.
   - `kill` and `raise` POSIX async-signal-safety is
     confirmed by the mini gate (not pre-decided in
     DESIGN).

### Source audit (no allocation in handler)

We do NOT use "no leak symptoms" as a criterion.  The mini
gate validates the handler by source audit:

- `readelf -Ws` (or `nm -D -u`) on the DSO shows no
  *undefined dynamic imports* the handler might reach.
  Note: the DSO as a whole DOES legitimately import
  `dlsym`, `dl_iterate_phdr`, and `__cxa_atexit` for the
  constructor / dlopen-interpose path; this is expected
  and does NOT mean the handler can reach them.  We audit
  the **handler's symbol's call graph**, not the DSO's
  import table.
- The audit produces a list of *defined* symbols the
  handler can call, taken from the handler's
  disassembly's call/jump targets.  Each target is
  checked against the allowed list: `write`,
  `kill`/`raise`, `_exit`, the custom hex/decimal writer.
  Any target outside this list fails the gate.
- The full DSO may import loader APIs (for constructor /
  dlopen); this is OK because the handler is in a
  different function and the DSO's loader API calls
  happen only in the constructor / dlopen paths, not
  in the handler's call graph.

## Execution plan (gated)

**This turn (per user authorization): only Step 2 (uninstrumented
baseline 3 runs) is executed. Steps 3-8 are deferred. No DSO /
mini gate source is created in this turn. No code changes. No build
of `out/qnx_release/libGLESv2.so` or `cefsimple`. The baseline
command from debf52a63 is reused unchanged. The baseline binary
is `debf52a63` as is.**

### Step 1 (this document)
Produce DESIGN.md.  STOP.  Await review.

### Step 3 (after baseline 3 runs confirm reproducibility, DEFERRED)
Build the diagnostic DSO and mini gate in `/tmp/`:

- `q++ -Vgcc_ntox86_64 -fexceptions -fno-rtti -fvisibility=default -fPIC -shared -o exit139_diag.so exit139_diag.cc`
- `q++ ... -o exit139_mini_gate exit139_mini_gate.cc -L /tmp -l exit139_diag -Wl,-rpath,/tmp`
  (mini gate links the DSO).
- Copy to `out/qnx_release/`.

### Step 2 — Uninstrumented baseline 3 runs (THIS TURN's scope)
Run the **baseline smoke command 3 times, WITHOUT the
LD_PRELOAD DSO**.  Per run, save serial transcript to
`/tmp/exit139-uninst-run{N}.log`.  Build the 9-observation
table per run.

Reproducibility gate:
- 0/3 runs show `exit_code=139` → **STOP**, condition
  not reproducible.
- 1/3 shows 139 → run 2 more (total 5).  If 2/5 or worse
  show 139, proceed.  If still 1/5, **STOP**.
- 2/3 or more show 139 → proceed directly.
- Any run shows `std::terminate invoked` > 0 or
  `exit_code=134` > 0 → **STOP**, recursive-mutex fix
  regression.
- Variation across runs is recorded.  We require
  enough structure to attribute the crash to a specific
  GPU process attempt.

### Step 4 — Mini gate + Inheritance gate (DEFERRED, post Step 2)
Build a mini gate binary `exit139_mini_gate.cc` in /tmp:

- Installs the same SIGSEGV handler as the DSO.
- In constructor, calls `dl_iterate_phdr` → emits
  `[MAP]` lines.
- Deliberately dereferences NULL.
- Run via:
  ```bash
  ./tools/qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 -- \
    'env LD_PRELOAD=/mnt/nfs/out/qnx_release/exit139_diag.so \
     /mnt/nfs/out/qnx_release/exit139_mini_gate 2>&1; echo __PI_QNX_EXIT__:$?'
  ```
- Run a parent-mini-gate-spawn-child test that:
  1. parent links the DSO,
  2. parent forks+execs child with `LD_PRELOAD` in
     env (simulates browser → GPU child inheritance via
     `posix_spawnp`),
  3. child emits `[MAP]`, then deliberately faults,
  4. child exits 139, parent exits 0.
- **Inheritance gate pass criteria**:
  1. DSO LOADED message appears in child,
  2. `[MAP]` lines from child constructor with child's
     pid,
  3. `[QNX-SIG] pid=... sig=11 ... PC=0x... SP=0x...`
     appears in child,
  4. child `exit_code == 139`,
  5. parent's `exit_code == 0`.
- Failure of any criterion → **STOP**, do NOT proceed
  to instrumented runs.

### Step 5 — Instrumented runs (3+ with LD_PRELOAD) (DEFERRED, post Step 4)
Run the **smoke command 3 times WITH the LD_PRELOAD DSO**.
Per run, save serial transcript to
`/tmp/exit139-inst-run{N}.log`.  Build the same 9-
observation table per run.  Same reproducibility gate
(0/3 STOP, 1/3 → +2 runs, 2/3 proceed, terminate/134
regression STOP).

**Instrumentation delta** (recorded in RESULT, NOT a
discard):
- Reproducibility difference (inst vs uninst).
- Time-to-crash distribution.
- Whether the `[QNX-SIG]` line was emitted by the
  crashed GPU process.
- Whether the crash DSO+offset is identifiable.

### Step 6 — Identify fault PC (DEFERRED, post Step 5)
- For each reproducer (uninst and inst), if a
  `[QNX-SIG]` line was emitted, the absolute PC is
  captured.  Host-side: look up PC in the unstripped
  binary in `out/qnx_release/` using:
  1. `nm -C` symbol size,
  2. `objdump -d` (disassembly at PC, not full dump),
  3. `addr2line` (debug info present in chromium build).
- Adopt a DSO+offset identification only when at least
  two of (nm size, objdump context, addr2line output)
  agree.  If none agree → mark PC as UNKNOWN.
- Do NOT speculate on the cause before the PC is
  identified.

### Step 7 (after PC identified, DEFERRED)
- State fault PC's DSO + function + offset as FACT.
- 1 to 3 hypotheses from surrounding source + event
  sequence.  No pre-decided cause.
- For each candidate, propose a **single-variable
  diagnostic experiment** (e.g. DSO-level skip of one
  feature path; each is a diagnostic observation, NOT a
  "fix" to mask the problem).  No commit of experimental
  changes.
- Each experiment: one instrumented smoke run,
  transcript, observation table vs instrumented
  baseline.  ≤ 3 experiments.
- Each experiment is a temporary local DSO change in
  /tmp; we revert before the next.

### Step 8 (after Step 7 or on dead end, DEFERRED)
- Write `RESULT.md` (untracked, next to this DESIGN.md):
  - Uninstrumented reproducibility (how many of N
    runs).
  - Instrumented reproducibility + instrumentation
    delta.
  - Identified PC (or UNKNOWN).
  - Function, source line.
  - Event sequence.
  - Hypotheses tried, accepted, rejected.
  - Unknowns.
  - Suggested follow-up actions and risk.
- Remove the diagnostic DSO from `out/qnx_release/`.
  Remove all `/tmp/exit139-*` files.  Remove
  `/tmp/throw_qnx_v4/` (leftover from prior work; not
  owned by this investigation).
- Verify: main worktree at `debf52a63` HEAD, `git status
  -s` matches the pre-investigation state (untracked
  files from prior investigations are read-only and
  stay), `out/qnx_release/libGLESv2.so` and `cefsimple`
  byte-for-byte identical to the post-bootstrap
  `debf52a63` build.
- **NO commit.**  Stop.  Report.

## Stop conditions (immediate)

- Recursive-mutex fix regression, terminate_capture_qnx
  regression, angle_unittests regression, gate-step
  failure, pre-investigation status conflict, baseline
  binary byte change, or any design question with no
  obvious answer → STOP and report.

## Safety guards

- Diagnostic DSO source and mini gate source: in `/tmp`,
  NOT in CEF repo or chromium tree.
- `out/qnx_release/libGLESv2.so`, `cefsimple`, all CEF-
  managed patches, ANGLE `BUILD.gn`, `libGLESv2.gni`:
  **read-only** during the investigation.  No edits.
- Existing untracked files in CEF repo (`prelim` /
  `unsafe` / v2 / v3 / v4 / `.pi-subagents` /
  `correlate.py` / `parse_gmd.py` / v2 source backups):
  read-only; not touched.
- `qnx_run.sh` reuses the existing tap0; no
  `qnx_setup_env.sh` re-run, no sudo.
- `rm -rf` restricted to `/tmp/exit139-*`, the DSO copy
  in `out/qnx_release/` (after investigation), and
  `/tmp/throw_qnx_v4/`.  No `rm` of CEF repo files or
  chromium tree files.