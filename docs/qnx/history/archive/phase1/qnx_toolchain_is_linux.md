# QNX toolchain is_linux patch results

## Changes Made

**File:** `build/toolchain/qnx/BUILD.gn`

Added `is_qnx = true` and `is_linux = true` to toolchain_args:
```gn
toolchain_args = {
  forward_variables_from(invoker_toolchain_args, "*")
  current_os = "qnx"
  is_qnx = true
  is_linux = true
  is_clang = true
}
```

## gn gen Result

**Status:** FAILED (but toolchain args were accepted)

```
ERROR at //components/crash/core/common/BUILD.gn:35:1: Assertion failed.
assert(use_crash_key_stubs || use_crashpad_annotation || is_castos)
```

## Analysis

Toolchain parsing succeeded. `is_linux = true` was accepted by GN.

The failure is **downstream** - a component-level assertion in `crash/core/common/BUILD.gn`:
- `is_castos = false` (QNX is not ChromeOS)
- `use_crash_key_stubs` and `use_crashpad_annotation` are not set for QNX

This requires additional build config fixes in the crash component or a QNX-specific default.gn.