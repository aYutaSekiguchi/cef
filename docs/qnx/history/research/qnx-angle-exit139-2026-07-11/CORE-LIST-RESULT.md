# CORE-LIST-RESULT (2026-07-12)

## Goal

Read-only catalog of `/var/dumper/` cores, host-side recovery test
(via existing NFS mount), and QNX-guest-side ELF header inspection
to determine loadability.

## Method

`qnx_run.sh --virgl --kill-existing --timeout 60 --boot-timeout 180`,
guest command: list `/var/dumper`, extract name/size/mtime/PID-sig
hint per file, dump `xxd -l 64` of `sh.core` for ELF magic, copy
`sh.core` to host via existing NFS mount at
`/mnt/nfs/out/qnx_release/exit139-core-candidates/sh.core`.

Raw: `/tmp/exit139-core-list-run1.log` (109 lines)

## FACT

### All cores in `/var/dumper/` (guest)

| File | Size (bytes) | Mtime |
|---|---:|---|
| angle_end2end_tests.core | 124391424 | 2026-06-05 08:02 |
| base.core | 140787712 | 2026-05-14 05:50 |
| base_unittests.core | 35323904 | 2026-06-29 19:04 |
| **cefsimple.core** | **335204352** | **2026-07-11 23:55:09** |
| ceftests.core | 453316608 | 2026-07-02 01:49 |
| content_shell.core | 223739904 | 2026-07-10 15:25 |
| content_shell.stripped.core | 198705152 | 2026-07-04 07:59 |
| drm-virtio.core | 0 | 2026-05-14 11:25 |
| **exit139_mini_gate.core** | **3342336** | **2026-07-11 22:10:40** |
| gdb.core | 2805596160 | 2026-06-29 14:15 |
| lottie-player.core | 23068672 | 2026-07-11 23:30 |
| mojo_unittests.core | 33259520 | 2026-06-12 15:35 |
| ozone_demo.core | 12255232 | 2026-07-03 13:35 |
| qnx_angle_egl_gles_egl_full_repro.core | 33947648 | 2026-07-10 15:16 |
| qnx_dmabuf_export_only_probe.core | 30474240 | 2026-07-02 14:45 |
| qnx_dmabuf_export_producer.core | 45481984 | 2026-07-02 13:45 |
| qnx_dmabuf_hangtest.core | 27197440 | 2026-07-02 13:50 |
| qnx_egl_extension_probe.core | 27262976 | 2026-07-02 12:47 |
| **sh.core** | **14680064** | **2026-07-11 23:38** |
| swiftshader_reactor_llvm_unittests.core | 31023104 | 2026-06-04 12:59 |
| swiftshader_reactor_subzero_unittests.core | 13565952 | 2026-06-04 12:59 |
| swiftshader_system_unittests.core | 12070912 | 2026-06-04 12:59 |
| test_utils_unittest_helper.core | 6598656 | 2026-06-11 23:41 |
| v8_hello_world.core | 587595776 | 2026-05-29 22:24 |
| v8_unittests.core | 641335296 | 2026-06-11 08:59 |

Total: 25 cores. PID/signal info **not embedded** in filenames
(grep for `pid|sig|signal|exit` returned no matches). QNX core
file naming follows process command name (`sh`, `cefsimple`,
`content_shell`, etc.).

### Host-side copy: **WORKS**

`cp -p /var/dumper/sh.core /mnt/nfs/out/qnx_release/exit139-core-candidates/sh.core`
succeeded. File at host:

```
/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/sh.core
14680064 bytes
sha256 = 99e1dff604ff2b03a349651472c42c0b4b407f183a62ecca4ad0876efe812581
```

### ELF header (host readelf on sh.core)

| | Value |
|---|---|
| Magic | `7f 45 4c 46 02 01 01 00` |
| Class | ELF64 |
| Data | 2's complement, little endian |
| OS/ABI | UNIX - System V |
| Type | **CORE (Core file)** |
| Machine | x86-64 |
| Program headers | 63 (NOTE + 62 LOAD) |
| Section headers | 0 |

`file sh.core`: `ELF 64-bit LSB core file, x86-64, version 1 (SYSV)`

### Note segment (PT_NOTE) content

QNX-specific note types present:

| Owner | Size | Description |
|---|---:|---|
| QNX | 0x9d0 | QNX core sysinfo (registers, thread list, etc.) |
| QNX | 0x110 | QNX core info (process info) |
| QNX | 0x604 | QNX link map (DSO list with paths) |

### Link map strings (highlights from PT_NOTE 0x604)

The link map contains the process's loaded DSO paths, including:

- `/usr/bin/sh`
- `exit139_sinterpose.so` (sinterpose DSO from prior turn)
- `/mnt/nfs/out/qnx_release/`
- `libregex.so.1`, `libreadline.so.8`, `libhistory.so.8`
- `/usr/lib/libreadline.so.8.3`, `lapic`, `pckbd`, `devi-*`

This **identifies the crashed process** as the sinterpose mini gate
run from the prior turn (2026-07-11 23:38), NOT a GPU SIGSEGV run.

## Implications

- **sh.core** is from the **sinterpose mini gate stack overflow**,
  not from a GPU SIGSEGV. The link map contains `exit139_sinterpose.so`.
- **`cefsimple.core`** (335204352 bytes, 2026-07-11 23:55:09) is the
  most likely candidate for the WAITSTATUS run, but the link map has
  not been inspected yet to confirm.
- **Host-side readelf on QNX cores works** — `sh.core` ELF64 type
  CORE, machine x86-64, parses cleanly with the QNX readelf. The
  PT_NOTE / QNX-specific notes parse via `-n` flag.
- **NFS-based host recovery works** — existing
  `/mnt/nfs/out/qnx_release/exit139-core-candidates/` mount path
  can carry QNX core files to host for offline symbolization.
- **Filename does NOT contain PID/signal info** on QNX. Process
  identification requires either link-map string inspection or
  cross-correlation with log timestamps.

## UNKNOWN

- **Process identification for `cefsimple.core`** — not yet
  inspected; would require host-side `readelf -n` to read link
  map strings and confirm it matches the WAITSTATUS run.
- **Process identification for `exit139_mini_gate.core`** — likely
  mini gate crash from the prior session (2026-07-11 22:10); not
  inspected.
- **Whether any of these cores correspond to a real GPU SIGSEGV**
  — would require: (a) link map containing `libGLESv2.so.1` or
  similar GPU-stack libs, AND (b) cross-check with ABC-RESULT
  log timestamps to find a matching GPU child crash.

## Tracking policy

- Per supervisor directive: no GPU reproduction run, no TLS
  investigation, no kernel changes, no tracked source changes,
  no commits.
- Host-side readelf/file inspection on existing cores is read-only
  and authorized.

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications.
0 commit, 0 push.

## Artifacts

| | Path |
|---|---|
| Run raw | `/tmp/exit139-core-list-run1.log` |
| Host-recovered core | `/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/sh.core` (14680064 bytes, sha256 99e1dff604ff2b03a349651472c42c0b4b407f183a62ecca4ad0876efe812581) |
| Previous notes | `docs/qnx/history/research/qnx-angle-exit139-2026-07-11/CORE-PROBE-NOTE.md`, `CORE-PROBE-RESULT.md` |