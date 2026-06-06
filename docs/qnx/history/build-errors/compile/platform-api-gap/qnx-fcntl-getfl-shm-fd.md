# QNX fcntl(F_GETFL) misreports shared memory descriptor access mode

- Date: 2026-05-22
- Signature: Unexpected(4) = TakeError::kUnexpectedReadOnlyFd
- Stage: compile
- Category: platform-api-gap
- Scope: base/memory

## Symptoms

- `PlatformSharedMemoryRegionTest.TakeOrFailWritable` failed with `Unexpected(4)`.
- `PlatformSharedMemoryRegionTest.TakeOrFailUnsafe`, `TakeOrFailReadOnly`, `MappingProtectionSetCorrectly`, and `CheckPlatformHandlePermissionsCorrespondToMode` also failed.
- Shared memory handles that should have been writable were treated as read-only.

## Root cause

- `CheckFDAccessMode` in `platform_shared_memory_region_posix.cc` uses `fcntl(F_GETFL)` to distinguish `O_RDONLY` from `O_RDWR`.
- On QNX, shared memory file descriptors reported `0` through `F_GETFL`, which looked like `O_RDONLY`.
- That broke permission validation and cascaded into mapping-related tests.

## Fix pattern

- Do not reuse Linux `F_GETFL` validation logic on QNX shared memory descriptors without proving the kernel/libc contract matches.
- Prefer a QNX-specific bypass or alternate validation path when the API surface is present but semantically incompatible.
- If a test depends on procfs or mapping-introspection semantics that are absent on QNX, gate or skip it explicitly.

## Applied change

- Guarded `CheckFDAccessMode` with `#if !BUILDFLAG(IS_QNX)`.
- Added a QNX path in `CheckPlatformHandlePermissionsCorrespondToMode` that returns success without relying on `fcntl`.
- Added QNX guards in the unit test for `TakeOrFail*`, `CheckPlatformHandlePermissionsCorrespondToMode`, and `MappingProtectionSetCorrectly`.
- Evaluated `fstat()` as a substitute and rejected it because mode bits were not compatible with `O_ACCMODE`.

## Verification

- All 8 relevant `PlatformSharedMemoryRegionTest` cases passed after the guards were added.

## Files touched

- `base/memory/platform_shared_memory_region_posix.cc`
- `base/memory/platform_shared_memory_region_unittest.cc`

## Related notes

- `docs/qnx/build-error-index.md`
