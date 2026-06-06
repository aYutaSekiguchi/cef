# GTest death test cwd fd becomes invalid after QNX spawn

- Date: 2026-05-23
- Signature: exit 134 / ENOTDIR from fchdir(cwd_fd)
- Stage: test
- Category: runtime-assumption
- Scope: third_party/googletest, base/test

## Symptoms

- Death-test batches crashed with exit 134 and skipped the remaining tests in the batch.
- `BackupRefPtrTest.Advance` crashed in launcher mode.
- The parent process aborted after `fchdir(cwd_fd)` failed with `ENOTDIR`.

## Root cause

- GTest's QNX death-test path saved the current directory using `open(".", O_RDONLY)` before `spawn()`.
- After `spawn()`, QNX invalidated that directory fd in the parent process.
- `fchdir(cwd_fd)` then failed, which triggered `GTEST_DEATH_TEST_CHECK_` and aborted the parent.

## Fix pattern

- Avoid assuming a directory fd remains valid across `spawn()` on QNX.
- For cwd preservation around process creation, prefer `getcwd()` plus `chdir()` when the fd-based contract is not portable.
- Reinforce death-test child setup with explicit CLOEXEC handling for auxiliary descriptors.

## Applied change

- Replaced `open(".") / fchdir() / close()` with `getcwd() / chdir()` in the death-test flow.
- Set `FD_CLOEXEC` on the XML output fd.
- Added `RemoveCloseOnExec()` for redirected stdio fds on QNX.
- Tried removing `--test-launcher-output` from death-test child argv and reverted that part due to side effects.

## Verification

- `BackupRefPtrTest.Advance` passed in about 110 seconds.
- `WeakPtrDeathTest.*` passed.
- Death-test infrastructure stabilized without the global abort.

## Files touched

- `third_party/googletest/src/googletest/src/gtest-death-test.cc`
- `base/test/gtest_xml_unittest_result_printer.cc`
- `base/test/test_timeouts.cc`
- `cef/patch/patches/qnx/googletest_death_test.patch`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/launch-qnx-posix-spawn-too-many-close-actions.md`
