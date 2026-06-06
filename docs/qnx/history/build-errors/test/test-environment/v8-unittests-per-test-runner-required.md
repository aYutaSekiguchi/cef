# v8_unittests on QNX must run one test per process

- Date: 2026-05-31
- Signature: v8_unittests SIGTRAP on repeated fixture runs with WithDefaultPlatformMixin
- Stage: test
- Category: test-environment
- Scope: V8 unittest runner strategy

## Symptoms

- `v8_unittests` crashed with SIGTRAP when multiple fixture-based tests ran in one process.
- Individual tests passed under `--gtest_filter`.

## Root cause

- `WithDefaultPlatformMixin` initializes and disposes the V8 platform per test instance.
- V8 startup state is not resettable within a single process after disposal.

## Fix pattern

- Mirror upstream per-test invocation strategy when a test suite assumes process isolation for fixture lifecycle.

## Applied change

- Added `cef/tools/qnx_run_v8_unittests.py` to enumerate tests and launch each one in its own process while keeping QEMU alive.

## Verification

- Full per-test run reached `6299/6324 PASS` and converted the crash into a manageable residual-fail set.

## Files touched

- `cef/tools/qnx_run_v8_unittests.py`
- `v8/test/unittests/testcfg.py`
- `v8/test/unittests/test-utils.h`
- `v8/src/init/v8.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/platform-api-gap/v8-stack-limit-clamp-for-qnx-thread-stacks.md`
