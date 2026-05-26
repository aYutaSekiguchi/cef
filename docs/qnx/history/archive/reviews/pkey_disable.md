# QNX pkeys Disable Check

## Files Retrieved
1. `<CHROMIUM_SRC_ROOT>/base/allocator/partition_allocator/partition_alloc.gni` (lines 497-503)

## Key Code
```gni
# pkeys support is explicitly disabled in all Cronet builds, as some test
# dependencies that use partition_allocator are compiled in AOSP against a
# version of glibc that does not include pkeys syscall numbers.
is_pkeys_available =
    (is_linux || is_chromeos) && current_cpu == "x64" && !is_cronet_build && !is_qnx
declare_args() {
  enable_pkeys = is_pkeys_available
}
```

## Architecture
- `is_pkeys_available` gates pkeys support on: Linux/ChromeOS, x64 CPU, not Cronet, **not QNX**
- `enable_pkeys` defaults to `is_pkeys_available` via `declare_args()`
- QNX already excluded via `!is_qnx` condition on line 498

## Conclusion
**No changes needed.** QNX is already handled: `enable_pkeys` will be `false` on QNX because `is_pkeys_available` evaluates to `false` when `is_qnx` is true.

## Supervisor coordination
- `reason: "progress_update"` — finding: already implemented