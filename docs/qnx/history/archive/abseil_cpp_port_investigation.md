# abseil-cpp QNX Port Investigation Report

## Version Comparison

| Repository | Branch/Version | Commit | Base LTS |
|------------|----------------|--------|----------|
| QNX Port | `qnx_20240116.0` | `302bcd51` | 20240116 |
| Chromium | Modified (LTS-based) | `8b958e72ec1ad` | Likely 20240725+ |

**Key Finding**: QNX port is based on **LTS 20240116**, approximately 6+ months behind current Chromium abseil-cpp.

## QNX-Specific Code Modifications

### 1. Platform Detection in `absl/base/config.h` (line 421)

```cpp
#elif defined(__linux__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(_AIX) || defined(__ros__) || defined(__native_client__) ||       \
    defined(__EMSCRIPTEN__) || defined(__Fuchsia__) ||                     \
    defined(__sun) || defined(__ASYLO__) || defined(__myriad2__) ||         \
    defined(__HAIKU__) || defined(__OpenBSD__) || defined(__NetBSD__) ||    \
    defined(__QNX__) || defined(__VXWORKS__) || defined(__hexagon__)
#define ABSL_HAVE_MMAP 1
```

Enables `mmap`, `pthread`, `sched_yield`, `semaphore` for QNX.

### 2. Stack Consumption Detection in `absl/debugging/internal/stack_consumption.h` (line 28)

```cpp
#elif !defined(__APPLE__) && !defined(_WIN32) &&                     \
    !(defined(__QNX__) && __QNX__ < 800) && \  // QNX-specific: disable for QNX < 8.0
```

### 3. Failure Signal Handler in `absl/debugging/failure_signal_handler.cc` (line 63)

```cpp
!(defined(TARGET_OS_TV) && TARGET_OS_TV) && !defined(__QNX__)
```

### 4. ELF Image Handling in `absl/debugging/internal/elf_mem_image.h` (line 34)

```cpp
#if defined(__ELF__) && !defined(__OpenBSD__) && !defined(__QNX__) && \
```

### 5. Raw Logging in `absl/base/internal/raw_logging.cc`

QNX added to platform list where FD_CLOEXEC is not available.

### 6. Random Platform in `absl/random/internal/platform.h`

QNX does not allow AES - hardware AES is disabled.

## Build System Files (in `qnx/` folder)

| File | Purpose |
|------|---------|
| `qnx/build/Makefile` | Recursive make entry point |
| `qnx/build/common.mk` | CMake-based build configuration |
| `qnx/build/qnx.nto.toolchain.cmake` | QNX toolchain (qcc/q++) setup |
| `qnx/build/project_hooks.cmake` | Links `libmuslflt` for floating-point consistency |

### Toolchain Settings (`qnx.nto.toolchain.cmake`)

- Compiler: `qcc` / `q++`
- C++ Standard: **gnu++11** (C++11, not C++14+)
- Target flags: `-Vgcc_nto${CMAKE_SYSTEM_PROCESSOR}`

## QNX Patches Summary

| Commit | Description | Files Modified |
|--------|-------------|----------------|
| `302bcd51` | Initial QNX 8.0 port | config.h, CMakeLists.txt, etc. |
| `b09c2564` | Disable stack consumption for QNX < 800 | stack_consumption.h |
| `d938d3c7` | Add QNX to raw_logging platform list | raw_logging.cc |
| `d539d236` | Enable dependency discovery CMake config | - |
| `323f7b3b` | Remove qnx/ folder (moved to build-files) | - |

## C++23 Support Status

**No C++23-specific patches in QNX port.** The toolchain is configured for C++11 only (`-std=gnu++11`).

## Chromium Integration Proposal

### Option 1: Cherry-Pick QNX Patches to Current abseil-cpp

Extract these core changes:
1. `absl/base/config.h` - Add `__QNX__` to platform detection macros
2. `absl/debugging/internal/stack_consumption.h` - Add QNX < 800 check
3. `absl/debugging/failure_signal_handler.cc` - Add QNX exclusion
4. `absl/debugging/internal/elf_mem_image.h` - Add QNX exclusion
5. `absl/base/internal/raw_logging.cc` - Add QNX to platform list
6. `absl/random/internal/platform.h` - Disable hardware AES for QNX

### Option 2: Maintain Separate QNX Branch

Keep `third_party/abseil-cpp` on a QNX-specific branch with:
- Backported patches from upstream
- GN build rules for Chromium integration
- TEST.gn for QNX test execution

### Required Additional Work

1. **Replace CMake with GN**: QNX port uses CMake; Chromium uses GN/Bazel
2. **Update C++ Standard**: Move from C++11 to C++14 minimum for modern abseil-cpp
3. **Test Suite Integration**: Map `qnxtests.sh` tests to GN equivalents
4. **Update LTS Base**: Upgrade from 20240116 to current LTS version

## Risks and Open Questions

1. **Version Gap**: ~6 month gap between QNX port and current Chromium abseil-cpp
2. **Build System Mismatch**: CMake vs GN requires significant adaptation
3. **C++ Standard**: C++11 in QNX port may conflict with abseil-cpp C++14+ requirements
4. **muslflt Dependency**: Floating-point library may be needed for consistent behavior
5. **Test Infrastructure**: CI/CD for QNX tests not integrated with Chromium's infrastructure