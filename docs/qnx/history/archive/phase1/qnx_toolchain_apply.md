# QNX Toolchain Apply Results

## Files Modified

1. `build/toolchain/qnx/BUILD.gn` - Rewrote with GN-compatible syntax
   - GN doesn't support ternary operator (`? :`)
   - GN doesn't support nested scope dereference (`invoker.toolchain_args.current_cpu`)
   - Workaround: local variable copy pattern

2. `build/config/clang/BUILD.gn` - Already contains `is_qnx` branch (lines 216-224)

## gn gen Result

```
ERROR at //ui/menus/BUILD.gn:6:1: Assertion failed.
assert(is_win || is_mac || is_linux || is_chromeos || is_android ||
       is_fuchsia || is_qnx)
```

### Root Cause
Chromium codebase has platform OS assertions that don't include `is_qnx`:
- `ui/menus/BUILD.gn:6`
- `components/crash/core/common/BUILD.gn:35`
- `build/config/cast.gni:108`

### What Works
- GN parses toolchain definition correctly
- Toolchain args (`current_os = "qnx"`, `is_clang = true`) set correctly
- `is_qnx` variable propagates to config files

### What's Broken
Full Chromium build requires broader codebase changes to support QNX as a target OS.

## Recommendation
Option A: Add `is_qnx` to all platform OS assertions (extensive, 50+ files)
Option B: Set `is_qnx = true` and `current_os = "linux"` (hacky but minimal)
Option C: Target specific Chromium components instead of full browser build

## Current Toolchain Settings
- Compile: bundled clang with `--target=x86_64-unknown-nto --sysroot=$QNX_TARGET`
- Link: QNX qcc with `-Vgcc_ntox86_64_cxx`
- CRT/libgcc: Handled by qcc (no compiler-rt needed)