# BackgroundCompileTask helper must honor its stack_size parameter

- Date: 2026-06-02
- Signature: CompileFailure ignored requested 100 KB stack size and overflowed C++ stack
- Stage: test
- Category: runtime-assumption
- Scope: V8 background compile tests

## Symptoms

- `BackgroundCompileTaskTest.CompileFailure` hit a C++ stack overflow path on QNX instead of a parser stack-limit failure.

## Root cause

- The helper function took a `stack_size` parameter but passed `v8_flags.stack_size` to the constructor instead.
- The dead parameter made the test silently use a much larger parser stack than intended.

## Fix pattern

- When a test helper exposes a tuning parameter, ensure it is actually threaded through before attributing failures to platform-only stack limits.

## Applied change

- Replaced the constructor argument from `v8_flags.stack_size` to `stack_size`.

## Verification

- The requested 100 KB parser stack limit took effect and `CompileFailure` behaved as intended on QNX.

## Files touched

- `v8/test/unittests/tasks/background-compile-task-unittest.cc`
- `cef/patch/patches/qnx/chromium/v8_background_compile_stack_size_qnx.patch`
- `cef/tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/test/platform-api-gap/v8-stack-limit-clamp-for-qnx-thread-stacks.md`
