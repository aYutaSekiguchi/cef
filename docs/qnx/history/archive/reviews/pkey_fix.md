# QNX pkeys Disable Confirmation

## File
`base/allocator/partition_allocator/partition_alloc.gni` (line 498-499)

## Content
```gni
is_pkeys_available =
    (is_linux || is_chromeos) && current_cpu == "x64" && !is_cronet_build && !is_qnx
declare_args() {
  enable_pkeys = is_pkeys_available
}
```

## Result
- `!is_qnx` condition already present
- On QNX, `is_pkeys_available = false` → `enable_pkeys = false`
- No fix needed (already addressed)