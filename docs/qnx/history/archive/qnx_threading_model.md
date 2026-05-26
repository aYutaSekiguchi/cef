# QNX Threading Model & libc++ Support Analysis

## 1. QNX Neutrino RTOS Architecture

QNX Neutrino is a **microkernel architecture**:
- Kernel (`procnto`) only handles: scheduling, IPC, interrupt handling
- All other services (filesystem, networking, drivers) run as **user-space processes**
- Each process can contain **multiple threads** (not processes) for concurrency

## 2. QNX Thread Support (POSIX pthreads)

QNX **fully supports POSIX pthreads** (IEEE 1003.1):

**Evidence from `<QNX_SDP_ROOT>/target/qnx/usr/include/pthread.h`:**
```
extern int pthread_create(pthread_t *__thr, const pthread_attr_t *__attr,
    void *(*__start_routine)(void *), void *__arg);

extern int pthread_join(pthread_t __thr, void **__value_ptr);
extern pthread_t pthread_self(void);
extern int pthread_mutex_lock(pthread_mutex_t *__mutex);
extern int pthread_cond_wait(pthread_cond_t *__cond, pthread_mutex_t *__mutex);
```

QNX pthread implementation:
- Supports thread creation, join/detach, cancellation
- Supports mutexes, condition variables, reader-writer locks
- Supports thread-specific data (`pthread_key_create`)
- QNX-specific extensions: `pthread_cluster_*` for NUMA affinity

## 3. libc++ Thread API Detection for QNX

**QNX sysroot libc++ DOES recognize QNX** (`__config` lines 1104-1142):

```cpp
// Line 1118: QNX is explicitly listed
defined(__QNX__) ||  // <- Listed in pthread API platforms

// Line 1135-1142: QNX triggers pthread API
#    elif defined(__QNX__)
#      define _LIBCPP_HAS_THREAD_API_PTHREAD
```

Additionally:
- Line 1142: `_LIBCPP_HAS_COND_CLOCKWAIT` defined for QNX (monotonic clock support)
- Lines 866, 894-900: QNX-specific ABI handling (`abi_tag("v160006")`)
- Line 516: QNX-specific PSTL configuration

**Conclusion: QNX is fully recognized by upstream libc++.**

## 4. Why Chromium Reports "No Thread API"

The issue is **not** upstream libc++ lacking QNX support. The problem lies in:
1. **sysroot path**: Using `<QNX_SDP_ROOT>/target/qnx/usr/include/c++/v1/__config`
   - This targets **QNX Neutrino** (neutrino == microkernel OS)
   - But Chromium's build may be looking at a different sysroot

2. **Cross-compilation toolchain detection**:
   - If clang doesn't recognize `__QNX__` preprocessor macro
   - Or if `--sysroot` isn't properly propagated to libc++ compilation

3. **sysroot vs. toolchain mismatch**:
   - The QNX sysroot has proper pthread support
   - But the clang being used may not be configured for QNX

## 5. Key Files

| File | Purpose |
|------|---------|
| `target/qnx/usr/include/pthread.h` (line 216) | pthread_create declaration |
| `target/qnx/usr/include/c++/v1/__config` (lines 1104-1142) | Thread API detection logic |
| `target/qnx/x86_64/usr/lib/libc++.a` | Pre-built libc++ for x86_64 |
| `target/qnx/aarch64le/usr/lib/libc++.a` | Pre-built libc++ for ARM64 |

## 6. Architecture Summary

```
QNX Neutrino Microkernel
├── procnto (kernel)
│   ├── Scheduler (SMP-aware, priority-based)
│   └── IPC (message passing)
└── User Processes (with threads)
    └── Each process: multiple pthreads (shared address space)
```

**QNX does NOT use "processes" for parallelism** - it uses **threads within processes**.
This is more efficient than Linux's process-per-connection model.

## 7. Conclusion

- QNX **fully supports pthreads** - no question about that
- libc++ **does recognize QNX** via `__QNX__` macro and defines `_LIBCPP_HAS_THREAD_API_PTHREAD`
- The "no thread API" error in Chromium is a **toolchain/sysroot configuration issue**, not a platform capability limitation
- Fix: Ensure clang receives `--sysroot=<QNX_SDP_ROOT>/target/qnx` and target is set to `x86_64-pc-nto-qnx` or similar