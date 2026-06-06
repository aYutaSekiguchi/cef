# QNX FD remapping needs fcntl-based close scanning instead of /dev/fd enumeration

- Date: 2026-05-23
- Signature: ProcessUtilTest.FDRemapping inherited extra parent FDs
- Stage: test
- Category: runtime-assumption
- Scope: base/process

## Symptoms

- `ProcessUtilTest.FDRemapping` regressed after removing `close_superfluous_fds`.
- Child processes inherited extra parent file descriptors.
- The earlier Linux-style `/dev/fd` scan was already unusable on QNX due to NFS and spawn-side `EBADF` issues.

## Root cause

- The initial fix for QNX spawn `EBADF` removed the broad close loop, which stopped over-closing but also stopped pruning unrelated inherited descriptors.
- Relying on `/dev/fd` enumeration was not robust on QNX and conflicted with the need to keep spawn file actions small.

## Fix pattern

- When broad descriptor cleanup is needed on QNX, probe descriptor validity with `fcntl(fd, F_GETFD)` instead of assuming `/dev/fd` is reliable.
- Keep an explicit keep-set for remap sources, remap targets, and stdio.
- Avoid the close loop entirely when there is no remap work, because spawn-internal descriptors may otherwise be disturbed.

## Applied change

- Switched close scanning to a `getdtablesize()` plus `fcntl(F_GETFD)` walk.
- Closed only open descriptors not present in `keep_fds`.
- Preserved remap sources, remap targets, and `stdin/stdout/stderr`.
- Skipped the close loop when `fds_to_remap` was empty.

## Verification

- `ProcessUtilTest.FDRemapping` passed.
- `FDRemappingIncludesStdio`, `EnsureTerminationUndying`, and `EnsureTerminationGracefulExit` also passed.

## Files touched

- `base/process/launch_qnx.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/launch-qnx-posix-spawn-too-many-close-actions.md`
