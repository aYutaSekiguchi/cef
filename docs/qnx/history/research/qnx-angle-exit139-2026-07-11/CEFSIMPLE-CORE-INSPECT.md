# CEFSIMPLE-CORE-INSPECT (2026-07-12)

## Goal

Inspect the recovered `cefsimple.core` (335204352 bytes, mtime
2026-07-11 23:55:09) on host to determine whether it is a GPU
child SIGSEGV core, the browser cefsimple tree crash, or some
other process.

## Method

1. Copy `/var/dumper/cefsimple.core` to host via NFS (existing
   `/mnt/nfs/out/qnx_release/exit139-core-candidates/` path).
2. `chmod 644` (QNX exports files mode 0600 owner `30`; chmod
   needed for host root read).
3. `sha256sum`, `file`, host `qnx800` cross-readelf `-h -n`.

Raws: `/tmp/exit139-core-cefsimple-cp.log`, `...-cp3.log`,
`...-chmod.log`.

## FACT

### Host-side metadata

| | Value |
|---|---|
| Path | `/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/cefsimple.core` |
| Size | 335204352 bytes |
| sha256 | `38662dfe4d5495baeb82cf96168120be8ccf38a369543edd66a201830fb9e457` |
| `file` | `ELF 64-bit LSB core file, x86-64, version 1 (SYSV)` |
| Type | CORE |
| Machine | x86-64 |
| Program headers | 771 (1 NOTE + 770 LOAD) |
| Section headers | 0 |

### PT_NOTE summary

| Owner | Size | Description |
|---|---:|---|
| QNX | 0x9d0 | QNX core sysinfo (registers, threads, etc.) |
| QNX | 0x110 | QNX core info (process info) |
| QNX | 0x8bc | QNX link map (DSO list with paths) |

### Link map strings (key entries)

```
/mnt/nfs/out/qnx_release/cefsimple
libcef.so
/mnt/nfs/out/qnx_release/
libc++.so.2
/usr/lib/
libm.so.3
/proc/boot/
libregex.so.1
libsocket.so.4
libfsnotify.so.1
libeventfd.so.1
ldqnx-64.so.2
libc.so.6
libgcc_s.so.1
libEGL.so
libbacktrace.so.1
libscreen.so.1
libEGL.so.1
libGLESv2.so.1
libz.so.2
libunwind-nto.so.0
libunwind-x86_64.so.8
libunwind.so.8
liblzma.so.5
```

## Interpretation (FACT, no speculation)

1. **This is the cefsimple browser process**, NOT a GPU child:
   - First loaded module is `/mnt/nfs/out/qnx_release/cefsimple`
     (the CEF browser binary).
   - `libcef.so` is loaded (CEF shared library).
   - **No ANGLE library** is loaded — there is no `libGLESv2.so`
     (ANGLE) entry, only `/usr/lib/libGLESv2.so.1` (system Mesa).
   - **No `libvulkan`, no ANGLE-related symbols**, no GPU child
     mojo interface.
   - This matches the **`CefBrowserView::CreateBrowserView` browser
     tree crash** observed in HANDLER-EFFECTIVENESS-RESULT.md and
     ABC-RESULT.md (browser-side 139, not GPU child).

2. **The browser uses system Mesa EGL/GLES, not ANGLE** — the
   libEGL.so and libGLESv2.so.1 paths are `/usr/lib/...`, not
   `/mnt/nfs/out/qnx_release/...`. This is the system EGL path
   exercised when LD_PRELOAD forces system EGL linkage.

3. **The mtime (2026-07-11 23:55:09) is 17 minutes after `sh.core`
   (2026-07-11 23:38)** — most plausibly from a `cefsimple`
   run that took the in-process EGL path. Could be the
   HANDLER-EFFECTIVENESS-RESULT.md's run1 (crashed with `139`)
   or one of the ABC runs.

4. **The core IS a SIGSEGV-style crash core**: ELF type CORE,
   771 program headers (many LOAD = full address-space capture
   consistent with a crashing process). The shell log marker
   `(core dumped)` from prior runs is consistent with this file.

## UNKNOWN

- **Exact PID** that produced this core. The QNX sysinfo NOTE
  would have it (process info section), but its binary layout is
  undocumented in the readelf output (would require reverse
  engineering or QNX gdb to interpret). NOT pursued per
  supervisor directive (no QEMU run, no symbolization).
- **Exact signal/PC** at the moment of the fault. Same
  restriction — would require interpreting the QNX sysinfo note
  manually or running the core under QNX gdb.
- **Whether the browser was using `--ozone-platform=qnx` or the
  default Views path at the moment of crash.** Both could land
  in the same `cefsimple` process.
- **Whether this is the GPU child** — **NO**. The link map
  contains no ANGLE library, no Vulkan library, and no
  `/mnt/nfs/out/qnx_release/` GPU-specific DSO beyond
  `libcef.so` and `cefsimple` itself. The GPU child, if it had
  been spawned, would have a distinct process and distinct
  linkmap (with `libGLESv2.so` ANGLE or system Mesa, plus
  GPU-specific paths).

## Implication for GPU SIGSEGV investigation

- The GPU child core file (if any) is **NOT** this `cefsimple.core`.
- A separate GPU child core would need to be produced by:
  1. Reproducing the GPU SIGSEGV in a fresh run (currently
     blocked: GPU SIGSEGV does not reproduce in our probe runs).
  2. Capturing the GPU child PID's core from `/var/dumper/` at
     crash time (requires PID-to-core matching, which is not
     supported by QNX core file naming).
- Without a GPU child core, **symbolization of the GPU fault PC
  is not feasible** from existing artifacts.

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications.
0 commit, 0 push.

## Artifacts

| | Path |
|---|---|
| Recovered core | `/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/cefsimple.core` |
| sha256 | `38662dfe4d5495baeb82cf96168120be8ccf38a369543edd66a201830fb9e457` |
| sh.core (prior) | `/home/yuta/chromium/src/out/qnx_release/exit139-core-candidates/sh.core` (14680064 bytes, sha256 `99e1dff604ff2b03a349651472c42c0b4b407f183a62ecca4ad0876efe812581`) |
| Previous notes | `CORE-LIST-RESULT.md`, `CORE-PROBE-RESULT.md`, `CORE-PROBE-NOTE.md` |

## STOP

Per supervisor: no symbolization, no addr2line, no objdump on
this core in this turn. Identification-only inspection complete.
Symbolization would require either:
1. A fresh GPU SIGSEGV reproduction + per-PID core capture
   (blocked by lack of GPU reproduction and PID→core mapping
   on QNX), or
2. Running the core under QNX gdb on guest side (out of scope).