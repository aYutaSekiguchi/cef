# QNX sysroot libc++ Usage Investigation

## Investigation Results

### 1. `use_custom_libcxx` Behavior (`build/config/c++/c++.gni`)

```gn
declare_args() {
  use_custom_libcxx = true  # Default is true
}
```

- `use_custom_libcxx = true` → Use Chromium bundled libc++ (buildtools/third_party/libc++)
- `use_custom_libcxx = false` → Use system/toolchain C++ library
- **Important**: This flag is deprecated and will be removed (M138)

### 2. Bundled libc++ Inclusion (`build/config/c++/BUILD.gn`)

`//build/config/c++:runtime_library` config sets:

```gn
cflags_cc += [
  "-nostdinc++",                                      # Exclude system includes
  "-isystem" + rebase_path("$libcxx_prefix/include", root_build_dir),  # Bundled headers first
  "-isystem" + rebase_path("$libcxxabi_prefix/include", root_build_dir),
]
```

At link time:
```gn
if (is_clang) {
  ldflags += [ "-nostdlib++" ]  # Exclude default library
}
```

`libcxx_prefix` = `//third_party/libc++/src` (use_clang_modules=false) or gen/third_party/libc++/src

### 3. QNX Toolchain Configuration (`build/toolchain/qnx/BUILD.gn`)

Current settings:
```gn
_target_flags = "--target=${_target} -D__QNXNTO__ ... --sysroot=${_qnx_target}"
extra_cflags = _target_flags
extra_cppflags = _target_flags
extra_cxxflags = _target_flags
extra_ldflags = "-Vgcc_ntox86_64_cxx"
```

## Solution

### Option A: Set `use_custom_libcxx = false` (Recommended)

In `args.gn`:
```
use_custom_libcxx = false
use_custom_libcxx_for_host = true  # Exception: host build still uses libc++
```

This:
1. Prevents `-nostdinc++` and `-nostdlib++` flags from being added
2. sysroot libc++ is automatically used

Since the QNX toolchain already sets `--sysroot=${_qnx_target}`, QNX libc++ is automatically resolved.

### Option B: Explicitly Specify sysroot libc++ in QNX Toolchain

Modify `build/toolchain/qnx/BUILD.gn`:
```gn
_extra_cxxflags = "--sysroot=${_qnx_target}"
_extra_ldflags = "-L${_qnx_target}/x86_64/usr/lib -lc++"

if (invoker.use_sysroot_libcxx == true) {
  _extra_cxxflags += " -nostdinc++ -isystem ${_qnx_target}/usr/include/c++/v1"
}
```

### Option C: Force sysroot libc++ Headers with Priority (-isystem)

Override in toolchain's `extra_cxxflags` instead of reading `c++.gni`:
```
extra_cxxflags = "--sysroot=${QNX_TARGET} -nostdinc++ -isystem ${QNX_TARGET}/usr/include/c++/v1"
```

## Key Points

1. `use_custom_libcxx = true` (default) → Adds `-nostdinc++` and `-nostdlib++` to explicitly exclude system libc++
2. `use_custom_libcxx = false` removes these flags, allowing sysroot libc++ to be used
3. QNX toolchain already sets `--sysroot`, so headers and libraries are automatically resolved

## Recommended Settings

```gn
# args.gn
use_custom_libcxx = false
use_custom_libcxx_for_host = true
```

Alternatively, set directly in `build/toolchain/qnx/BUILD.gn` so only this toolchain uses sysroot libc++.