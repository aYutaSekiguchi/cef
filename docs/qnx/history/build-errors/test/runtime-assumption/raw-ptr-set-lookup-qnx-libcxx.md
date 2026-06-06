# raw_ptr set-lookup expectations differ under QNX libc++ std::set internals

- Date: 2026-05-23
- Signature: get_for_comparison_cnt expected 2 actual 4
- Stage: test
- Category: runtime-assumption
- Scope: partition_alloc raw_ptr tests

## Symptoms

- `RawPtrTest.SetLookupUsesGetForComparison` observed more comparisons than Linux expected.

## Root cause

- QNX libc++ `std::set` internals performed different comparison patterns and used `std::less` rather than the spaceship path assumed by the test.

## Fix pattern

- Where a test asserts exact comparison counts from standard-library internals, expect platform-specific branches when the standard does not guarantee those internals.

## Applied change

- Added QNX-specific expected counts for `set.emplace(ptr)` and `set.count(ptr)`.

## Verification

- `RawPtrTest.SetLookupUsesGetForComparison` passed on QNX.

## Files touched

- `base/allocator/partition_allocator/src/partition_alloc/pointers/raw_ptr_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
