# QNX death-test children must bypass SetUp skip logic tied to launcher output

- Date: 2026-05-23
- Signature: DCHECK death tests exit 0 because SetUp calls GTEST_SKIP
- Stage: test
- Category: runtime-assumption
- Scope: base/test gtest death tests

## Symptoms

- Several DCHECK death tests exited normally with status 0 instead of crashing as expected.

## Root cause

- Death-test child argv intentionally omitted `--test-launcher-output`.
- Test `SetUp()` methods interpreted that omission as a reason to `GTEST_SKIP()`.
- The death-test body never executed, so there was no crash to observe.

## Fix pattern

- Teach setup code to distinguish real top-level runs from death-test child runs when command-line stripping is expected infrastructure behavior.

## Applied change

- Added `::testing::internal::InDeathTestChild()` checks in affected `SetUp()` methods and returned early for death-test children.

## Verification

- All 5 affected tests passed after the `InDeathTestChild()` guard was added.

## Files touched

- `base/test/gtest_links_unittest.cc`
- `base/test/gtest_sub_test_results_unittest.cc`
- `base/test/gtest_tags_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
