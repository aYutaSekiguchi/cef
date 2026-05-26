# QNX libc++ and Chromium Integration Research

## 1. QNX Sysroot libc++ Status

### Current Situation: **NOT AVAILABLE**

The QNX SDP 8.0 sysroot (`~/qnx800/target/qnx/usr/`) contains:
- **libstdc++ (GCC)** at version 12.2.0 (`__GLIBCXX__ 20220819`)
- **No libc++** library files (.so, .a) found anywhere in the sysroot
- C++20 headers are present in `include/c++/12.2.0/` with C++23 feature tests (e.g., `__cplusplus > 202002L` checks)

### GCC libstdc++ 12.2.0 C++ Support
- Full C++20 support
- Partial C++23 support: `std::expected`, `std::span`, `std::format` (partial), `std::string_view`
- C++23 standard library components available via feature-test macros
- **NOT a full libc++ implementation** — this is GNU libstdc++

### Assessment
QNX does NOT ship libc++. The sysroot only has GCC libstdc++. For Chromium to use libc++ on QNX, you would need to either:
1. Build and install LLVM libc++ for QNX (separate project)
2. Use QNX's GCC libstdc++ with appropriate compatibility flags

---

## 2. Chromium libc++ Configuration and Disabling

### Key Files
- `build/config/c++/c++.gni` (lines 16-29) — `use_custom_libcxx` argument definition
- `build/config/c++/BUILD.gn` — libc++ build configuration
- `build/toolchain/qnx/BUILD.gn` — QNX-specific toolchain setup

### How to Disable Custom libc++ (Use System/Compiler Default)

**GN arg to set:**
```
use_custom_libcxx = false
```

**In your `args.gn`:**
```python
use_custom_libcxx = false
use_custom_libcxx_for_host = false
```

### What Happens When Disabled

From `c++.gni` (lines 192-203):
```python
if ((!use_custom_libcxx || !use_custom_libcxx_for_host) &&
    build_with_chromium &&
    current_toolchain == default_toolchain) {
  print("*********************************************************************")
  print("WARNING: Support for linking against a C++ standard library other ")
  print("  than the one in-tree (buildtools/third_party/libc++) is deprecated")
  print("  and support for this will end. We plan to remove this option in ")
  print("  M138.")
  print("*********************************************************************")
}
```

### libc++ Prefix Behavior (c++.gni lines 120-154)
When `use_clang_modules = true` (default for supported configs):
- `libcxx_prefix = "${root_build_dir}/gen/third_party/libc++/src"`
- `libcxxabi_prefix = "//third_party/libc++abi/src"`

When modules disabled:
- `libcxx_prefix = "//third_party/libc++/src"`

### Alternative: Target sysroot libc++
Chromium has no built-in mechanism to point at a sysroot libc++. If QNX had libc++, you would need to:
1. Create a custom `//build_overrides/build.gn` that sets `libcxx_prefix` to the sysroot path
2. Override `libcxxabi_prefix` similarly
3. Disable `use_custom_libcxx` to prevent Chromium's bundled libc++ from being used

---

## 3. abseil-cpp QNX Port Analysis

### QNX Port Details
- **Repository**: https://github.com/qnx-ports/abseil-cpp
- **Branch/Tag**: `qnx_20240116.0`
- **Latest commit**: `424eafa` — "Add test install subdir for clarity"
- **Build system**: CMake with QNX-specific toolchain file (`qnx.nto.toolchain.cmake`)

### QNX-Specific Configuration
- **Compiler**: Clang (QNX QCC)
- **C++ Standard**: C++17 default (matches upstream abseil)
- **Testing**: Uses GoogleTest (`gmock` required)
- **Install prefix**: `usr/local`

### C++23 Support
- **No C++23-specific patches** found in the QNX port
- Uses standard upstream abseil-cpp CMake configuration
- Option `ABSL_PROPAGATE_CXX_STD ON` enables C++ standard propagation

### Key Differences from Chromium's abseil-cpp
| Aspect | QNX Port | Chromium |
|--------|----------|----------|
| Build system | CMake (external) | GN/Bazel (in-tree) |
| Version | qnx_20240116.0 | Rolling (Chromium-managed) |
| C++ std | C++17 default | C++20 (via use_cxx20/use_cxx23) |
| Testing | Standalone with gtest | Chromium test infrastructure |

### Integration Approach for Chromium
Chromium bundles its own abseil-cpp at `third_party/abseil-cpp/`. To use QNX's abseil:
1. **Not recommended**: Replace bundled abseil with QNX port
   - GN build system mismatch
   - Version drift from Chromium's integration
   - Test infrastructure incompatibility

2. **Alternative**: Build QNX abseil as external library
   - Keep `third_party/abseil-cpp/` for Chromium's internal use
   - Build QNX abseil separately for target device
   - Use pre-built binaries for production

---

## 4. Recommended Next Steps for QNX Chromium Build

### If Using GCC libstdc++ (Current QNX SDK)
```python
# args.gn
use_custom_libcxx = false
target_os = "qnx"
target_cpu = "arm64"  # or "x64"
```

### If Building libc++ for QNX (Future)
1. Clone https://github.com/llvm/llvm-project
2. Build libc++ for QNX target using LLVM's standard build process
3. Install to QNX sysroot `usr/lib/` and `usr/include/c++/`
4. Create `//build_overrides/build.gn` to set `libcxx_prefix`

### QNX Toolchain Configuration
From `build/toolchain/qnx/BUILD.gn`:
- Uses QCC linker (`qcc` instead of `clang++`)
- Sets `--target=aarch64-unknown-nto` or `--target=x86_64-unknown-nto`
- Uses `--sysroot=${QNX_TARGET}` for headers/libraries

---

## Summary

| Item | Status | Action Required |
|------|--------|-----------------|
| QNX sysroot libc++ | **NOT AVAILABLE** | Need to build/install LLVM libc++ for QNX |
| QNX sysroot libstdc++ | **AVAILABLE** | GCC 12.2.0, C++20+ partial support |
| Disable Chromium's libc++ | **POSSIBLE** | Set `use_custom_libcxx = false` |
| abseil-cpp QNX port | **AVAILABLE** | Separate build, not direct Chromium integration |
| QNX toolchain support | **PARTIAL** | Basic toolchain exists, needs libc++ work |