# QNX death-test batches need a longer default launcher timeout

- Date: 2026-05-22
- Signature: BackupRefPtrTest.Advance TIMEOUT at 45 seconds
- Stage: test
- Category: test-environment
- Scope: base/test launcher timeouts

## Symptoms

- Test batches with many death tests timed out under the default 45-second launcher timeout.
- `BackupRefPtrTest.Advance` timed out and blocked dependent tests.

## Root cause

- The QNX QEMU environment executed death-test batches significantly more slowly than the default timeout assumed.

## Fix pattern

- When the test environment is materially slower but semantically correct, raise the platform-default timeout instead of chasing false negatives one test at a time.

## Applied change

- Raised QNX default `test_launcher_timeout_` to 180 seconds in `base/test/test_timeouts.cc`.

## Verification

- Death-test batches completed instead of timing out at 45 seconds.

## Files touched

- `base/test/test_timeouts.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/runtime-assumption/googletest-death-test-cwd-fd-invalidation.md`
