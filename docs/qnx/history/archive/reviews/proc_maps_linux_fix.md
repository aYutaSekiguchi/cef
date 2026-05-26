# `proc_maps_linux.cc` QNX Build Exclusion Confirmation

## Investigation Target
`base/allocator/partition_allocator/src/partition_alloc/BUILD.gn`

## Current Build Condition (line 986)
```gn
if ((is_linux && !is_qnx) || is_chromeos || is_android) {
  source_set("debug_proc_maps") {
    sources = [
      "partition_alloc_base/debug/proc_maps_linux.cc",
      "partition_alloc_base/debug/proc_maps_linux.h",
    ]
  }
}
```

## Condition Interpretation

| Platform | `is_linux` | `is_qnx` | `is_chromeos` | `is_android` | Built |
|----------|------------|----------|---------------|--------------|-------|
| Linux (x64)    | true  | false    | false         | false        | ✓ |
| ChromeOS       | -     | -        | true          | -            | ✓ |
| Android        | -     | -        | -             | true         | ✓ |
| QNX            | false | **true** | false         | false        | ✗ |

**Evaluation:** `(is_linux && !is_qnx)` = `(false && !true)` = `false`

On QNX toolchain, `is_linux` is not set; only `is_qnx = true` is set.

## Conclusion

**No fix needed** — `!is_qnx` is already included in the condition, so `proc_maps_linux.cc` is not built on QNX.

## Note
Same pattern is used in `base/BUILD.gn` (line 1022):
```gn
if ((is_linux && !is_qnx) || is_chromeos) {
```