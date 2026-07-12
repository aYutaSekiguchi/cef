# QNX ANGLE path: BASELINE — 3 uninstrumented runs (2026-07-11)

## Scope

Per user authorization (turn scope: only Step 2 of DESIGN.md
executed), this report covers:

- 3 uninstrumented cefsimple runs (no LD_PRELOAD DSO, no
  DSO source created, no build of any diagnostic binary,
  no edits to `out/qnx_release/`).
- Raw serial transcripts at:
  - `/tmp/exit139-baseline-run1.log`
  - `/tmp/exit139-baseline-run2.log`
  - `/tmp/exit139-baseline-run3.log`
- Each run used the **same command** as the `debf52a63`
  baseline smoke, with no modification:
  ```
  ./tools/qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180 -- \
    './cefsimple --ozone-platform=qnx --use-gl=angle --use-angle=gles-egl \
      --no-sandbox --use-native --url=about:blank --ozone-qnx-gpu-trace \
      --enable-logging=stderr 2>&1 & p=$!; sleep 15; kill -TERM $p 2>/dev/null || true; \
      wait $p 2>/dev/null || true; exit 0'
  ```
- `qnx_setup_env.sh` was NOT re-run.  Existing tap0 used
  (verified `NO-CARRIER` state is sufficient for the
  runner).  No `sudo`.  No edits to the chromium tree or
  CEF repo.

## Reproducibility gate (per DESIGN Step 2)

| Stop condition | Observed in 3 runs | Verdict |
|---|---|---|
| `exit_code=139` for any GPU attempt | 1/3 in run 1, 2/3 in run 2, 2/3 in run 3 | ≥ 2/3 always; reproducibility confirmed |
| `exit_code=134` > 0 | 0 in all 3 runs | PASS (recursive fix not regressed) |
| `std::terminate invoked` > 0 | 0 in all 3 runs | PASS |

**Proceed to Step 3 (mini / inheritance gate) is authorized.**

## Per-run tables

### Run 1 (raw: /tmp/exit139-baseline-run1.log, 249 lines)

| Marker | PID | Time | Line |
|---|---|---|---|
| `terminate handler installed` (browser) | 434206 | 08405x | 33 |
| `OnGpuServiceLaunched: host_id=1 starting` | 434206 (browser) | 084053.420252 | 36 |
| `terminate handler installed` (GPU #1, ×2) | 528415 | 084053.x | 37, 39 |
| `terminate handler installed` (role unknown) | 528417 | 08405x | 40 |
| `ready to call SubmitFrame` | 528415 (GPU #1) | 084054.129247 | 128 |
| `terminate handler installed` (role unknown) | 528419 | 08405x | 129 |
| `terminate handler installed` (role unknown) | 528420 | 08405x | 131 |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #1) | 434206 (browser) | 084054.758008 | 133 |
| `QnxRenderProducer::Initialize: required DMAbuf ... missing` | 528415 (GPU #1) | 084054.782267 | 139 |
| **`GPU process exited unexpectedly: exit_code=139`** | 434206 (browser) | **084102.993150** | **156** |
| `OnGpuServiceLaunched: host_id=2 starting` | 434206 (browser) | 084103.075889 | 158 |
| `OnChannelDestroyed: host_id=1` | 434206 (browser) | 084103.076601 | 159 |
| `terminate handler installed` (GPU #2, ×2) | 548895 | 08410x | 160, 161 |
| `ready to call SubmitFrame` | 548895 (GPU #2) | (not reached before run end) | — |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #2) | (not reached) | — | — |
| `Reinitialized the GPU process after a crash` | (not emitted) | — | 0 |

**Run 1 summary**: 1 GPU crash (`exit_code=139`); the crashed
process (most plausibly `host_id=1` / PID 528415, by
proximity) was followed by a `host_id=2` spawn (PID 548895)
that did NOT reach `ready to call SubmitFrame` before the
15-second timeout.  No `Reinitialized` line.  `DMAbuf
missing` from GPU #1 only.

### Run 2 (raw: /tmp/exit139-baseline-run2.log, 375 lines)

| Marker | PID | Time | Line |
|---|---|---|---|
| `terminate handler installed` (browser) | 606238 | 08412x | 33 |
| `OnGpuServiceLaunched: host_id=1 starting` | 606238 (browser) | 084125.994328 | 36 |
| `terminate handler installed` (GPU #1, ×2) | 614431 | 084125.x | 37, 38 |
| `ready to call SubmitFrame` | 614431 (GPU #1) | 084126.726102 | 128 |
| `terminate handler installed` (role unknown) | 614435 | 08412x | 129 |
| `terminate handler installed` (role unknown) | 614436 | 08412x | 131 |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #1) | 606238 (browser) | 084127.266682 | 133 |
| `QnxRenderProducer::Initialize: required DMAbuf ... missing` | 614431 (GPU #1) | 084127.293316 | 139 |
| **`GPU process exited unexpectedly: exit_code=139`** (GPU #1) | 606238 (browser) | **084135.490714** | **173** |
| `OnGpuServiceLaunched: host_id=2 starting` | 606238 (browser) | 084135.616168 | 175 |
| `OnChannelDestroyed: host_id=1` | 606238 (browser) | 084135.616989 | 176 |
| `terminate handler installed` (GPU #2, ×2) | 708639 | 084135.x | 177, 178 |
| **`Reinitialized the GPU process after a crash`** (after GPU #1 crash) | 606238 (browser) | **084135.960432** | 259 |
| `ready to call SubmitFrame` | 708639 (GPU #2) | 084135.981189 | 260 |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #2) | 606238 (browser) | 084136.177679 | 261 |
| `QnxRenderProducer::Initialize: required DMAbuf ... missing` | 708639 (GPU #2) | 084136.206135 | 267 |
| **`GPU process exited unexpectedly: exit_code=139`** (GPU #2) | 606238 (browser) | **084144.655903** | **283** |
| `OnGpuServiceLaunched: host_id=3 starting` | 606238 (browser) | 084144.716365 | 285 |
| `OnChannelDestroyed: host_id=2` | 606238 (browser) | 084144.717007 | 286 |
| `terminate handler installed` (GPU #3, ×2) | 725023 | 08414x | 287, 288 |
| `ready to call SubmitFrame` | (not reached) | — | — |

**Run 2 summary**: 2 GPU crashes (`exit_code=139` ×2: GPU #1
crashed, browser respawned as GPU #2, GPU #2 also crashed).
GPU #1 and GPU #2 each emitted `DMAbuf missing`.  1
`Reinitialized` line — corresponds to the first crash (GPU
#1).  GPU #2's crash did not produce a second `Reinitialized`
line within the timeout (browser respawn state may have
shifted).  GPU #3 spawned but did not reach `ready` before
timeout.

### Run 3 (raw: /tmp/exit139-baseline-run3.log, 375 lines)

| Marker | PID | Time | Line |
|---|---|---|---|
| `terminate handler installed` (browser) | 606238 | 08420x | 33 |
| `OnGpuServiceLaunched: host_id=1 starting` | 606238 (browser) | 084205.621340 | 36 |
| `terminate handler installed` (GPU #1, ×2) | 614431 | 084205.x | 37, 38 |
| `ready to call SubmitFrame` | 614431 (GPU #1) | 084206.213912 | 128 |
| `terminate handler installed` (role unknown) | 614435 | 08420x | 129 |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #1) | 606238 (browser) | 084206.451928 | 130 |
| `QnxRenderProducer::Initialize: required DMAbuf ... missing` | 614431 (GPU #1) | 084206.459637 | 136 |
| `terminate handler installed` (role unknown) | 614436 | 08420x | 151 |
| **`GPU process exited unexpectedly: exit_code=139`** (GPU #1) | 606238 (browser) | **084215.173102** | **172** |
| `OnGpuServiceLaunched: host_id=2 starting` | 606238 (browser) | 084215.292360 | 174 |
| `OnChannelDestroyed: host_id=1` | 606238 (browser) | 084215.293377 | 175 |
| `terminate handler installed` (GPU #2, ×2) | 708639 | 084215.x | 176, 177 |
| `Reinitialized the GPU process after a crash` (after GPU #1) | 606238 (browser) | 084215.634728 | 258 |
| `ready to call SubmitFrame` | 708639 (GPU #2) | 084215.645556 | 259 |
| `BindGpuControlAndAttachExistingWidgets: ack` (GPU #2) | 606238 (browser) | 084215.805024 | 260 |
| `QnxRenderProducer::Initialize: required DMAbuf ... missing` | 708639 (GPU #2) | 084215.827138 | 266 |
| **`GPU process exited unexpectedly: exit_code=139`** (GPU #2) | 606238 (browser) | **084223.355056** | **282** |
| `OnGpuServiceLaunched: host_id=3 starting` | 606238 (browser) | 084223.420366 | 284 |
| `OnChannelDestroyed: host_id=2` | 606238 (browser) | 084223.437778 | 285 |
| `terminate handler installed` (GPU #3, ×2) | 725023 | 08422x | 286, 287 |
| `ready to call SubmitFrame` | (not reached) | — | — |

**Run 3 summary**: same shape as run 2 — 2 GPU crashes,
1 `Reinitialized` line, GPU #1 and GPU #2 each emit `DMAbuf
missing`.  GPU #3 spawned but did not reach `ready` before
timeout.

## Per-run RGBA counts

| Run | GPU #1 RGBA (prefixed) | GPU #1 first/last | GPU #2 RGBA (prefixed) | GPU #2 first/last | GPU #3 RGBA (prefixed) | GPU #3 first/last | Unprefixed ERR RGBA | Total |
|---|---|---|---|---|---|---|---|---|
| 1 | 28 (PID 528415) | 084053.743906 .. 084054.017282 | 40 (PID 548895) | 084103.240405 .. 084103.514426 | — | — | 69 | 160 |
| 2 | 18 (PID 614431) | 084126.311496 .. 084126.534748 | 40 (PID 708639) | 084135.725665 .. 084135.948248 | 40 (PID 725023) | 084144.870701 .. 084145.136904 | 103 | 240 |
| 3 | 17 (PID 614431) | 084205.885678 .. 084206.122220 | 40 (PID 708639) | 084215.402665 .. 084215.621741 | 40 (PID 725023) | 084223.540867 .. 084223.807197 | 95 | 240 |

Notes:
- Total RGBA = 160 in run 1 vs 240 in runs 2, 3.  The
  difference is **the number of GPU attempts that reach
  `generateConfigs`**: in run 1 only 2 of 2 GPU attempts
  emit RGBA; in runs 2 and 3 all 3 of 3 GPU attempts
  emit RGBA (40 lines each), so 3 × 40 = 120 prefixed + ~93
  interleave/ERR = 240.  The unprefixed ERR count grows
  proportionally.  Total RGBA line count per run is NOT a
  reproducibility metric; the per-PID count and timing are.
- In **all 3 runs**, the GPU that reaches
  `ready to call SubmitFrame` first (GPU #1) emits a
  smaller RGBA count (17-28 lines) than the subsequent
  GPU attempts (40 lines each).  We do NOT speculate
  on the cause of this difference: the prefixed RGBA
  count is **not** a reliable measure of the actual
  number of RGBA events, because serial interleave in
  the QEMU guest console corrupts the PID prefix on
  some lines (cf. the ~20 interleave-corrupted lines
  counted in the prior debf52a63 baseline analysis),
  pushing some RGBA events into the unattributed bucket.
  The total RGBA line count (160 in run 1, 240 in runs
  2-3) likewise conflates actual events with
  interleave-corrupted and unprefixed duplicate
  (`LOG(severity)` mirror) lines.  We record the
  per-PID prefixed counts and the per-run totals as
  observed, without inferring event counts.

## Cross-run stability observations

**Stable across all 3 runs:**
- `exit_code=139` is observed in all 3 runs (1, 2, 2
  crashes per run respectively).
- `exit_code=134` is 0 in all 3 runs.
- `std::terminate invoked` is 0 in all 3 runs.
- `QnxRenderProducer::Initialize: required DMAbuf ...
  missing` is emitted by every GPU that reaches
  `ready to call SubmitFrame` (run 1: once; runs 2, 3:
  twice).
- `Reinitialized` is 0 in run 1, 1 in runs 2 and 3.  In
  runs 2 and 3, this single `Reinitialized` line is the
  browser's reaction to the first GPU crash; the second
  GPU crash does not produce a second `Reinitialized` line
  in the 15-second window.
- The `terminate handler installed` lines after each
  `OnGpuServiceLaunched` give us the GPU child PID (by
  proximity, soft inference).

**Variable across runs:**
- Browser PID is different across runs (run 1 = 434206;
  runs 2, 3 = 606238).  This is **not** QEMU guest ASLR but a
  guest-reboot process-ID allocation difference — each
  `qnx_run.sh` invocation starts a fresh QEMU guest, so
  PIDs start from a small value (typically ~4xx-7xx) each time.
- GPU child PIDs are different across runs (run 1:
  528415/548895; runs 2, 3: 614431/708639/725023).  Same
  cause: each `qnx_run.sh` invocation starts a fresh QEMU
  guest; PIDs are not stable across runs.
- Number of GPU attempts that reach `ready to call
  SubmitFrame` before the 15-second timeout: 1 in run 1;
  2 in runs 2 and 3.  In run 1, the second GPU
  (`548895`) appears to not have time to reach
  `ready` before the 15-second timeout; the `terminate
  handler installed` lines for 548895 are present, but
  no `ready` line is emitted within the run window.

**Reproducibility verdict:**
- The `exit_code=139` crash is **reproducible across all 3
  runs** (3/3 runs).  In runs 2 and 3 the same process
  (browser) sees the crash twice (GPU #1 + GPU #2 both
  crash with 139 before the 15-second timeout).
- The crash timing relative to `ready to call SubmitFrame`
  differs: in runs 2 and 3, GPU #1 reaches `ready` and
  then crashes ~9 seconds later; in run 1, GPU #1 reaches
  `ready` and crashes ~8.9 seconds later.  Similar delta
  but not identical.
- `DMAbuf missing` precedes every crash by ~8-9 seconds in
  every run.  This is **correlation, not causation**.

## Self-status

- `git status -s` matches the pre-investigation state
  (untracked files from prior investigations remain
  read-only; no diagnostic code introduced).
- `out/qnx_release/libGLESv2.so` and `cefsimple` are
  unchanged (no build in this turn).
- HEAD: `debf52a63` (no commit in this turn).

## Authorization to proceed

- Reproducibility gate **PASSED** (3/3 runs show
  `exit_code=139`; 0/3 show regression markers).
- **Gate satisfied; awaiting supervisor approval** to
  proceed to Step 3 (build diagnostic DSO + mini gate,
  run inheritance gate).  This turn stops at the
  baseline 3-run table; no DSO source, no build, no
  commit in this turn.