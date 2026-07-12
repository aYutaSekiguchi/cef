# CORE-PROBE-RESULT (2026-07-12)

## Goal

Determine whether a QNX core file is generated when the GPU child
process dies with `WTERMSIG=11 WCOREDUMP=1` (raw status `0x8b`).
If a core is generated, where does it land, and is it loadable
for symbolization.

## Method

1. `qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180`
2. Guest command (no DSO, no LD_PRELOAD):
   ```
   ulimit -c unlimited 2>&1;
   echo PROC_DUMPER_PROBE_START;
   ls -la /proc/dumper 2>&1;
   ls -la /var/dumps /var/dumper /tmp /data /dev/shmem 2>/dev/null;
   echo PROC_DUMPER_PROBE_END;
   cd /mnt/nfs/out/qnx_release &&
   /mnt/nfs/out/qnx_release/cefsimple --ozone-platform=qnx
     --use-gl=angle --use-angle=gles-egl --no-sandbox --use-native
     --url=about:blank --ozone-qnx-gpu-trace --enable-logging=stderr;
   echo CORE_POST_PROBE_START;
   ls -la /var/dumps /var/dumper /tmp/core* /tmp/*core* /data/core*
     /data/*core* /dev/shmem/core* 2>/dev/null;
   find / -name 'core*' -newer /tmp 2>/dev/null | head -20;
   echo CORE_POST_PROBE_END
   ```
3. Raw: `/tmp/exit139-core-probe-run1.log` (176 lines)

## FACT (from this run)

### Guest-side probes (pre-cefsimple)

| Probe | Result |
|---|---|
| `ulimit -c unlimited` | (executed, no error returned) |
| `/proc/dumper` | exists, mode `nrw-r----- 1 30 30 0`, size 0, mtime 2026-07-11 23:54 |
| `/var/dumps` | does not exist (ls reported nothing) |
| `/var/dumper` | exists; contains 14 core files (6.5 MB – 641 MB), total ~11 GB |
| `/tmp` | symlink to `/data/var/tmp` |
| `/data` | empty (no top-level content) |
| `/dev/shmem` | empty |

### Pre-existing cores in `/var/dumper/` (from prior sessions)

| File | Size (bytes) | Mtime |
|---|---:|---|
| `qnx_angle_egl_gles_egl_full_repro.core` | 33947648 | 2026-07-10 15:16 |
| `qnx_dmabuf_export_only_probe.core` | 30474240 | 2026-07-02 14:45 |
| `qnx_dmabuf_export_producer.core` | 45481984 | 2026-07-02 13:45 |
| `qnx_dmabuf_hangtest.core` | 27197440 | 2026-07-02 13:50 |
| `qnx_egl_extension_probe.core` | 27262976 | 2026-07-02 12:47 |
| **`sh.core`** | **14680064** | **2026-07-11 23:38** |
| `swiftshader_reactor_llvm_unittests.core` | 31023104 | 2026-06-04 12:59 |
| `swiftshader_reactor_subzero_unittests.core` | 13565952 | 2026-06-04 12:59 |
| `swiftshader_system_unittests.core` | 12070912 | 2026-06-04 12:59 |
| `test_utils_unittest_helper.core` | 6598656 | 2026-06-11 23:41 |
| `v8_hello_world.core` | 587595776 | 2026-05-29 22:24 |
| `v8_unittests.core` | 641335296 | 2026-06-11 08:59 |
| (`/tmp/sh.core` is the same as `/var/dumper/sh.core` via symlink) | | |

### This run's GPU path

| | Count |
|---|---:|
| `[exit139-waitstatus]` markers | 0 |
| `GPU process exited unexpectedly: exit_code=139` markers | 0 |
| `raw=0x8b` markers | 0 |
| `trace trap` / `(core dumped)` markers | 0 |
| `OnGpuServiceLaunched: host_id=N starting` | 6 |
| `OnChannelDestroyed: host_id=N` | 4 |
| Wrapper exit reason | `TimeoutError: QNX command timed out after 60.0 seconds` (no `__PI_QNX_EXIT__:N` printed) |

### Cores written this run

- `/var/dumper/` mtime unchanged (2026-07-11 23:38).
- `find / -name 'core*' -newer /tmp` returned no new entries.
- **No new core file was written during this run.**

### Host-side access

- Guest filesystem `/data/var/dumper` is **not accessible from host**
  (NFS mount is the other direction; guest's `/data` is host's
  `/export/chromium-src`).
- Cannot compute sha256 or `file` for any guest-side core from host.

## UNKNOWN

- **What process the `sh.core` from 2026-07-11 23:38 belongs to.**
  Most plausibly the prior session's WAITSTATUS-diagnostic run
  (which ran with the same `sh -c '...cefsmple...'` form and
  produced `__PI_QNX_EXIT__:139`). Confirmed match would require
  readelf on the guest-side file (not accessible from host).
- **Why this run did not reproduce GPU SIGSEGV.** Possible:
  (a) guest state difference (recent boots, mount timing);
  (b) the inline `ulimit -c unlimited` and probe commands changed
  the runtime environment enough to perturb the GPU SIGSEGV path;
  (c) genuine non-determinism in the GPU child startup. Not
  investigated per supervisor directive.
- **Whether a `dumper` utility is configured to write into
  `/var/dumper` automatically** for every signal-11 death. The
  pre-existing cores are evidence that the mechanism works for
  SOME processes (test_utils_unittest_helper, v8_unittests, etc.),
  but the absence of new cores this run cannot distinguish
  "GPU SIGSEGV did not happen" from "dumper is selective".
- **Core file format / architecture / loadability.** Not
  determinable from host. Would need QNX-side `file`,
  `readelf -n`, or `dumpifs`.

## Implications

- The QNX guest **has a working core-dump mechanism**: `/proc/dumper`
  exists, `/var/dumper/` contains 14 cores from prior sessions.
- The most recent GPU-SIGSEGV-adjacent core is `sh.core`
  (14680064 bytes, 2026-07-11 23:38), likely from the WAITSTATUS
  diagnostic run.
- Future core-driven symbolization would require either:
  1. QEMU-side `readelf -n` / `dumpifs` on the core file (since
     host cannot access `/data/var/dumper`); or
  2. NFS-export of `/data/var/dumper` from guest (requires guest
     config change, which is out of scope per supervisor).

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications. 0 commit, 0 push.

## Artifacts

| | Path |
|---|---|
| Run raw | `/tmp/exit139-core-probe-run1.log` (176 lines) |
| Guest core (not host-accessible) | `/var/dumper/sh.core` (14680064 bytes, 2026-07-11 23:38) |
| Previous notes | `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/CORE-PROBE-NOTE.md` |