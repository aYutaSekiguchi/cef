# CORE-PROBE-NOTE (2026-07-12)

## Status

GPU SIGSEGV core artifact probe paused. **No QEMU run was
performed** in this turn. Long-form docs exploration was
cancelled per supervisor directive. Future probe would require
authorization.

## FACT (read-only / docs-based, partial)

- QNX docs mention `/proc/dumper` and `dumper` utility as the
  mechanism for capturing postmortem state of a process
  (`procnto.html` help page).
- No evidence of an explicit `procnto -c <corename>` flag in the
  current QEMU image build files (search did not complete
  thoroughly before being cancelled).
- No evidence of `ulimit -c` / `setrlimit(RLIMIT_CORE)` calls in
  chromium's `child_process_launcher_helper_qnx.cc` (read-only
  grep performed).
- Previous turn's runtime marker `(core dumped)` from QNX shell
  output (e.g., `__PI_QNX_EXIT__:139 ... (core dumped)`) is the
  shell's text-only indication of the WCOREDUMP bit; it does not
  confirm that a core file was actually written to disk.

## UNKNOWN

- Whether a core file is written to disk in the current QNX
  guest when a process dies with `WTERMSIG=11 WCOREDUMP=1`.
- Where the core file would land: `/var/dumps`, `/tmp`, `/data`,
  `/dev/shmem`, NFS mount, or some QNX-specific path.
- Whether the dumper utility is installed in the current guest
  image.
- Whether the QNX kernel is configured to honor core dumps for
  processes that exit via SIGSEGV (vs some QNX-specific class
  filter).

## Tracking policy

- Tracking paused. No further core-probe QEMU runs, kernel
  config changes, or symbolization work authorized at this
  time.
- Recorded here for future reference. If the question is
  reopened, the recommended probe is:
  1. Read-only docs search for `procnto -c <path>` flag and
     QNX core dump mechanism (~30 min, scope-limited).
  2. In a fresh QEMU run with `--virgl` and the same cefsimple
     flags, before launching cefsimple:
     - `cat /proc/boot/procnto-cmdline 2>/dev/null`
     - `ls -la /var/dumps /var/dumper /tmp /data 2>/dev/null`
     - `ulimit -c` (read current rlimit)
     - `which dumper`
  3. After exit139 reproduces:
     - `find / -name 'core*' -newer /tmp/start-marker 2>/dev/null`
     - `ls -la /var/dumps/ 2>/dev/null`
  4. If a core file is found, capture `file`, `size`, `sha256`,
     then `readelf -n` for notes and loadable segment summary.

## Cross-references

- WAITSTATUS-RESULT.md — established `0x8b` raw status pattern.
- ABC-RESULT.md — GPU SIGSEGV baseline reproducer.
- TLS-SIGTRAP-NOTE.md — TLS SIGTRAP tracking paused separately.

## Tracked source state

Unchanged. No CEF/Chromium tracked modifications.
0 commit, 0 push.