# Sysroot libc++ Build Test Results

## Summary
**FAIL** - QNX sysroot libc++ is NOT being used in compilation commands.

## Test Results

### 1. Compilation Command Analysis
Commands for QNX target contain:
```
--sysroot=<QNX_SDP_ROOT>/target/qnx --target=x86_64-unknown-nto
```

**MISSING** - No libc++ include paths:
- ❌ `-nostdinc++` is NOT present (good)
- ❌ `-isystem <QNX_SDP_ROOT>/target/qnx/usr/include/c++/v1/` is NOT present
- ❌ `-isystem <QNX_SDP_ROOT>/target/qnx/usr/include/c++/` is NOT present

### 2. Header Search Path Verification
When compiling with QNX sysroot manually:
```
ignoring nonexistent directory "<QNX_SDP_ROOT>/target/qnx/usr/local/include"
#include <...> search starts here:
 /usr/lib/llvm-18/lib/clang/18/include
 <QNX_SDP_ROOT>/target/qnx/usr/include
End of search list.

error: 'string' file not found
```

The QNX sysroot DOES have libc++ headers:
```
<QNX_SDP_ROOT>/target/qnx/usr/include/c++/v1/
  __algorithm/ __atomic/ __bit/ __charconv/ __chrono/ __compare/
  __concepts/ __condition_variable/ __coroutine/ ...
```

### 3. Build Output
```
[1/2] ACTION //cef:args_gn_source(//build/toolchain/qnx:clang_x64)
[2/2] AR obj/base/libbase_static.a
```
Succeeds because libbase_static.a is pre-built archive (no new C++ compilation).

## Root Cause
`build/toolchain/qnx/BUILD.gn` sets `--sysroot` but does NOT add libc++ include paths.

The gcc_toolchain template (`build/toolchain/gcc_toolchain.gni`) does not
automatically add libc++ paths for custom sysroots.

## Required Fix
In `build/toolchain/qnx/BUILD.gn`, add to `extra_cxxflags`:
```
_isystem_cxx = "-isystem${_qnx_target}/usr/include/c++"
extra_cxxflags = _target_flags + [ _isystem_cxx, "${_isystem_cxx}/v1" ]
```

Alternatively, check if GN's libc++ config system has a hook for this:
- `build/config/c++/c++.gni` has `use_custom_libcxx` and `libcxx_prefix`
- Currently no code path for "use sysroot libc++" without custom build

## Architecture Note
Chromium expects to build its own libc++ from `//third_party/libc++` when
`use_custom_libcxx=true`. For QNX sysroot, we need to:
1. Disable building custom libc++: `use_custom_libcxx = false`
2. Point to sysroot libc++: Add `-isystem` flags for `usr/include/c++/v1/`

## Files Involved
- `<CHROMIUM_SRC_ROOT>/build/toolchain/qnx/BUILD.gn` - needs libc++ include flags
- `<CHROMIUM_SRC_ROOT>/build/config/c++/c++.gni` - libc++ configuration
- `<CHROMIUM_SRC_ROOT>/build/toolchain/gcc_toolchain.gni` - toolchain template