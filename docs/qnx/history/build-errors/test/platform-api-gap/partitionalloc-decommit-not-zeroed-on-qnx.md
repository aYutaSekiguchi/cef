# QNX decommit does not guarantee zeroed memory on recommit

- Date: 2026-05-23
- Signature: PartitionAllocPageAllocatorTest.DecommitErasesMemory failed after recommit
- Stage: test
- Category: platform-api-gap
- Scope: partition_alloc page allocator

## Symptoms

- After `DecommitSystemPages()` and `RecommitSystemPages()`, memory was not zeroed on QNX.

## Root cause

- QNX `madvise(MADV_DONTNEED)` semantics differ from Linux and do not guarantee zeroed memory on recommit.

## Fix pattern

- Treat decommit-zeroing as a platform contract, not a universal POSIX property.
- Reflect the real OS behavior in the allocator capability function and let tests key off that.

## Applied change

- Changed `DecommittedMemoryIsAlwaysZeroed()` to return `false` on QNX.

## Verification

- `PartitionAllocPageAllocatorTest.DecommitErasesMemory` passed through the existing early-return path.

## Files touched

- `base/allocator/partition_allocator/src/partition_alloc/page_allocator.h`

## Related notes

- `docs/qnx/build-error-index.md`
