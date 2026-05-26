# BUILDFLAG(IS_LINUX) QNX Application Impact Analysis

## 1. BUILDFLAG(IS_LINUX) Usage

| Item | Count |
|------|-------|
| Total usages in base/ | 409 locations |
| Affected files | 96 files |
| Usages in sandbox/ | ~15 locations |

## 2. Dangerous Linux-Specific Features Enabled by IS_LINUX=true

### 2.1 Direct Dangerous syscall/Usage

| Feature | File:Line | Risk |
|---------|-----------|------|
| `prctl(PR_SET_NO_NEW_PRIVS)` | `base/process/launch_posix.cc:492` | Not supported on QNX |
| `prctl(PR_SET_PDEATHSIG)` | `base/process/launch_posix.cc:503` | Not supported on QNX |
| Linux syscalls (`SYS_rt_sigaction`) | `base/process/launch_posix.cc:154` | Syscall numbers may differ |
| inotify API | `base/files/file_path_watcher_inotify.cc` | Not available on QNX |
| seccomp BPF | `sandbox/policy/linux/` all | Not available on QNX |
| Linux magic numbers | `base/process/process_linux.cc` | procfs API |

### 2.2 Sandbox Features Guarded by IS_LINUX || IS_CHROMEOS

| Feature | File | QNX Compatibility |
|---------|------|-------------------|
| BPF seccomp policy | `sandbox/policy/linux/*.cc` | ❌ Incompatible |
| Seccomp features | `sandbox/policy/features.cc` | ❌ Incompatible |
| Linux namespaces | Verified unused | N/A |
| cgroups | Verified unused | N/A |
| perf_events | Verified unused | N/A |

### 2.3 Linux Kernel Header Includes

```cpp
#include <linux/magic.h>     // base/process/process_linux.cc
#include <linux/perf_event.h> // base/android/thread_instruction_count.cc
#include <linux/ashmem.h>     // base/android/linker/ashmem.cc
#include <linux/futex.h>      // partition_allocator (when HAS_LINUX_KERNEL)
#include <linux/version.h>    // partition_allocator/tagging.cc
```

---

## 3. Option A: Set BUILDFLAG(IS_LINUX)=1

### 3.1 build_config.h Change

```cpp
// Before (lines 91-95)
#if !defined(OS_CHROMEOS)
// Do not define OS_LINUX on Chrome OS build.
// The OS_CHROMEOS macro is defined in GN.
#define OS_LINUX 1
#endif  // !defined(OS_CHROMEOS)

// Option A: Also define OS_LINUX for __QNXNTO__
#if !defined(OS_CHROMEOS) && !defined(__QNXNTO__)
// Do not define OS_LINUX on Chrome OS build or QNX.
// The OS_CHROMEOS macro is defined in GN.
#define OS_LINUX 1
#endif  // !defined(OS_CHROMEOS) && !defined(__QNXNTO__)
```

**Lines changed: 1 added + 1 comment modified**

### 3.2 Dangerous Linux-Specific Code Enabled

| Category | Count | Risk Level |
|----------|-------|-----------|
| prctl calls | ~15 locations | ⚠️ Runtime error |
| seccomp BPF | ~8 files | ⚠️ Build error or runtime error |
| inotify | 1 file | ⚠️ Build error |
| Linux syscall numbers | ~10 locations | ⚠️ Syscall inconsistency |

### 3.3 Advantages

- ✅ Natural alignment between GN build flags and C++ macros
- ✅ No need to handle `IS_LINUX || IS_CHROMEOS` pattern
- ✅ Some POSIX-compatible code may work as-is

### 3.4 Disadvantages/Risks

| Risk | Impact |
|------|--------|
| prctl(PR_SET_NO_NEW_PRIVS) failure | Sandbox not fully disabled |
| prctl(PR_SET_PDEATHSIG) failure | Child process exit notification won't work |
| inotify API calls | `EOPNOTSUPP` runtime error |
| seccomp BPF policy | Always fails on QNX (unsupported API) |
| Linux syscall numbers | Unexpected behavior with different syscall numbers |
| Linux kernel headers not present | Build errors |

### 3.5 Worst Case

Setting IS_LINUX=1 on QNX causes this in `launch_posix.cc`:
```cpp
if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
  RAW_LOG(FATAL, "prctl(PR_SET_NO_NEW_PRIVS) failed");
}
```
This **always FATAL errors and crashes**.

---

## 4. Option B: Keep IS_LINUX=0 + Add IS_QNX

### 4.1 Files and Locations Requiring Changes

Currently problematic files with build errors:

| File | IS_LINUX Usages | Fix Pattern |
|------|-----------------|------------|
| `base/process/launch_posix.cc` | 12 locations | Pattern A |
| `base/rand_util_posix.cc` | 4 locations | Pattern A |
| `base/process/process_metrics_posix.cc` | 5 locations | Pattern A |
| `base/process/process_metrics.cc` | 4 locations | Pattern A |
| `base/files/file_path_watcher_inotify.cc` | 3 locations | Pattern A |
| `base/profiler/thread_delegate_posix.cc` | 2 locations | Pattern A |
| `base/memory/discardable_shared_memory.cc` | 1 location | Pattern A |
| `base/base_paths_posix.cc` | 1 location | Pattern A |

**Fix Pattern Classification:**

| Pattern | Meaning | Estimated Count |
|---------|---------|-----------------|
| **A**: `IS_LINUX || IS_QNX` | Treat QNX as equivalent to Linux | ~35 locations |
| **B**: `!IS_LINUX && !IS_QNX` | Exclude on both Linux and QNX | ~10 locations |
| **C**: `!IS_QNX` | Exclude only on QNX | ~5 locations |
| **D**: Whole-file exclusion | sources -= | ~3 files |

### 4.2 Fix Pattern Examples

**Pattern A: IS_LINUX || IS_QNX (Recommended)**
```cpp
// base/process/launch_posix.cc
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_AIX)
// → 
#if BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_AIX) || BUILDFLAG(IS_QNX)
```

**Pattern B: !IS_LINUX && !IS_QNX**
```cpp
// base/profiler/stack_base_address_posix.cc
#if !BUILDFLAG(IS_LINUX) && !BUILDFLAG(IS_QNX)
// Processing for non-Linux/non-QNX (BSD, etc.)
#endif
```

**Pattern C: !IS_QNX**
```cpp
// base/process/set_process_title.cc
#if !BUILDFLAG(IS_QNX)
// QNX-unnecessary/unsupported features
#endif
```

### 4.3 Advantages

- ✅ Dangerous Linux-specific APIs won't accidentally activate
- ✅ QNX-specific behavior can be explicitly controlled
- ✅ Runtime errors prevented
- ✅ IS_QNX patterns already partially exist (low learning curve)

### 4.4 Disadvantages

| Disadvantage | Impact |
|---------------|--------|
| Many fix locations | ~50-70 direct edits required |
| Increased test effort | Full pattern behavior verification needed |
| Higher maintenance cost | Risk of forgetting when new IS_LINUX added |
| Consistency risk | Boundary between IS_LINUX and IS_QNX may blur |

---

## 5. Quantitative Comparison

| Evaluation Item | Option A (IS_LINUX=1) | Option B (IS_QNX Added) |
|----------------|----------------------|------------------------|
| build_config.h changes | 2 lines | 0 lines |
| Files needing changes | 0 | ~15-20 files |
| Locations needing changes | 0 | ~50-70 locations |
| Dangerous API activation | ⚠️ 12+ locations | ✅ 0 |
| Runtime error possibility | 🔴 High | 🟢 Low |
| Sandbox safety | 🔴 Disabled/crashes | 🟢 Properly skipped |
| Short-term implementation cost | 🟢 Low | 🔴 High |
| Long-term maintenance cost | 🟢 Low | 🔴 Medium |

---

## 6. Recommended Option

### Recommended: **Option B (Keep IS_LINUX=0 + Add IS_QNX)**

### Rationale

1. **Safety first**: `prctl(PR_SET_NO_NEW_PRIVS)` always fails on QNX and FATAL errors. This single point is critical.

2. **Existential risk**: Setting IS_LINUX=1 can crash QNX processes immediately. Option B explicitly eliminates danger.

3. **Precedent exists**: `IS_LINUX || IS_QNX` patterns already implemented in multiple locations (`message_pump_for_ui.h`, `stack_base_address_posix.cc`, etc.).

4. **Incremental applicability**: Can first apply Pattern A, then add Patterns B/C as needed.

5. **QNX API integrity**: QNX has unique features like QNET/MPROC that differ from Linux. IS_LINUX=1 would suppress these capabilities.

### Implementation Steps

1. Do not change `build/build_config.h` (keep OS_LINUX undefined)
2. Apply `IS_LINUX → IS_LINUX || IS_QNX` replacement to major files
3. Exclude seccomp/sandbox systems from `IS_LINUX || IS_CHROMEOS`
4. After testing, additionally apply Patterns B/C

---

## 7. Appendix: build_config.h Current Structure

```cpp
// Lines 88-100
#elif defined(__linux__)
#if !defined(OS_CHROMEOS)
// Do not define OS_LINUX on Chrome OS build.
// The OS_CHROMEOS macro is defined in GN.
#define OS_LINUX 1
#endif  // !defined(OS_CHROMEOS)
// Include features.h for glibc/uclibc macros.
#include <features.h>
```

ChromeOS has `OS_CHROMEOS` pre-defined in GN, so the Linux definition is avoided by the `#if !defined(OS_CHROMEOS)` block. QNX has `__QNXNTO__` defined, so it doesn't enter this block (meaning OS_LINUX is undefined).
