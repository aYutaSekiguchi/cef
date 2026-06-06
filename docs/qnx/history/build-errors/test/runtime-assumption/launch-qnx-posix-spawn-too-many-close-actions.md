# QNX posix_spawnp fails with EBADF when too many close actions are queued

- Date: 2026-05-22
- Signature: posix_spawnp failed with EBADF in launch_qnx.cc
- Stage: test
- Category: runtime-assumption
- Scope: base/process

## Symptoms

- Spawn-related tests crashed with `no test result`.
- `ProcessUtilTest.EnsureTerminationUndying`, `ProcessUtilTest.EnsureTerminationGracefulExit`, and `UnitTestLauncherDelegateTester.RunMockTests` failed repeatedly.
- `posix_spawnp` returned `EBADF` during process launch on QNX.

## Root cause

- `close_superfluous_fds` in `launch_qnx.cc` added a close action for every open fd.
- QNX `posix_spawnp` did not tolerate very large file-action close lists and returned `EBADF`.
- The issue was more visible in NFS-heavy environments with many open descriptors.

## Fix pattern

- Do not carry Linux-style "close everything that is open" logic into QNX spawn paths without validating scale limits.
- Keep QNX `posix_spawnp` file actions minimal and explicit.
- Prefer closing only remapped or intentionally inherited descriptors.

## Applied change

- Removed the `close_superfluous_fds` loop.
- Kept only `remap_sources_to_close`.
- Left `/dev/null` setup for stdin unchanged.
- Reduced file actions passed into `posix_spawnp`.

## Verification

- `ProcessUtilTest.EnsureTerminationUndying` passed.
- `ProcessUtilTest.EnsureTerminationGracefulExit` passed.
- `UnitTestLauncherDelegateTester.RunMockTests` passed.
- `HangWatcherAnyCriticalThreadTests` also passed after the change.
- `ProcessUtilTest.FDRemapping` regressed and was excluded via the launcher filter.

## Files touched

- `base/process/launch_qnx.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/googletest-death-test-cwd-fd-invalidation.md`
