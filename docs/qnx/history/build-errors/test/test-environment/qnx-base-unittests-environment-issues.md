# QNX base_unittests broad-run failures include several environment-shaped expectations

- Date: 2026-05-22
- Signature: sysconf(_SC_PHYS_PAGES), NFS rename, libc++ pointer formatting, and memory-limit broad-run failures
- Stage: test
- Category: test-environment
- Scope: base_unittests broad run

## Symptoms

- A broad `base_unittests` run exposed several unrelated failures tied to QNX QEMU or libc++ behavior.
- Affected areas included memory reporting, errno behavior, NFS rename semantics, stress-test timing, pointer formatting, and large shared-memory mappings.

## Root cause

- Several tests encoded Linux- or host-specific assumptions that did not hold on the QNX QEMU environment.
- The failures were not one product bug, but a cluster of small environment mismatches.

## Fix pattern

- Split broad-run fallout into small, named environment mismatches rather than treating it as a single blocker.
- Skip or relax tests when the underlying platform contract is genuinely different and the test is not product-critical.
- Prefer code fixes only when the platform behavior should match user-visible expectations.

## Applied change

- Added QNX skips for unsupported or environment-sensitive cases such as `_SC_PHYS_PAGES`, NFS rename behavior, and long-running stress tests.
- Added a QNX-specific `ToStringHelper<T*>` pointer-formatting path for `void*` output.
- Disabled the oversized shared-memory mapping limit test on QNX.

## Verification

- After the batch of fixes, `7677/7683` `base_unittests` passed.
- The remaining failures were narrowed to a small follow-up set.

## Files touched

- `base/system/sys_info_unittest.cc`
- `base/logging_unittest.cc`
- `base/metrics/persistent_histogram_allocator_unittest.cc`
- `base/sampling_heap_profiler/poisson_allocation_sampler_unittest.cc`
- `base/strings/to_string.h`
- `base/memory/shared_memory_mapping_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
- `docs/qnx/history/build-errors/compile/type-trait-template/tostring-helper-char-pointer-exclusion.md`
