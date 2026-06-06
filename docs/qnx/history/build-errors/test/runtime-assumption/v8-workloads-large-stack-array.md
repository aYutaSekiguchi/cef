# WorkloadsTest.BasicFunctionality must move its 100000-pointer array off the stack

- Date: 2026-06-02
- Signature: WorkloadsTest.BasicFunctionality overflows QNX worker-thread stack at function entry
- Stage: test
- Category: runtime-assumption
- Scope: V8 cppgc workload tests

## Symptoms

- `WorkloadsTest.BasicFunctionality` overflowed the QNX worker-thread stack before the real test logic ran.

## Root cause

- The test allocated an approximately 800 KB pointer array on the stack.
- QNX worker threads only had about 256 KB of stack in this environment.

## Fix pattern

- For test bookkeeping structures that do not need stack storage, move them to heap-backed containers instead of weakening the test budget.

## Applied change

- Replaced the fixed stack array with a `std::vector` and preserved the original capacity semantics.

## Verification

- The test executed its intended cppgc allocation behavior on QNX instead of aborting on stack overflow.

## Files touched

- `v8/test/unittests/heap/cppgc/workloads-unittest.cc`
- `cef/patch/patches/qnx/chromium/v8_workloads_basic_functionality_stack_qnx.patch`
- `cef/tools/cef_create_projects_qnx.sh`

## Related notes

- `docs/qnx/build-error-index.md`
