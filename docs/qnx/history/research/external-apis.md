# External APIs Research - QNX SDP 8 Availability

## Summary

This document researches whether QNX SDP 8 provides equivalents for the 12 missing API symbols/headers encountered in the Chromium port.

---

## 1. sys/syscall.h

| Aspect | Finding |
|--------|---------|
| **QNX Availability** | **NO** |
| **Header** | N/A |
| **Linux syscall numbers** | QNX Neutrino does not expose Linux kernel syscall numbers via `sys/syscall.h` |
| **QNX Equivalent** | QNX uses `Connect server MsgsSendv()` style IPC for kernel services rather than raw syscalls. For syscall-style access, QNX provides `<fcntl.h>`, `<unistd.h>` with standard POSIX calls, but no direct syscall numbers |
| **Recommended Fix** | `#ifdef __QNXNTO__` guard + stub. The syscall numbers in `launch_posix.cc` and `rand_util_posix.cc` are Linux-specific (SYS_gettid, SYS_futex, etc.). QNX doesn't need these paths |
| **Confidence** | HIGH - QNX Neutrino architecture differs from Linux; no syscall.h equivalent |

---

## 2. linux/futex.h

| Aspect | Finding |
|--------|---------|
| **QNX Availability** | **NO** |
| **Header** | N/A |
| **QNX Equivalent** | QNX uses POSIX semaphore and mutex primitives with kernel-assisted wait |
| **Equivalent API** | `sem_t` + `sem_wait()`/`sem_post()` for futex-like operations, OR `pthread_mutex_t` with `pthread_mutex_lock()`/`pthread_mutex_unlock()` |
| **Rationale** | QNX's kernel provides priority inheritance on mutexes/semaphores natively. No userspace futex needed |
| **Recommended Fix** | `#ifdef __QNXNTO__` use `sem_t` backed semaphore; `#else` use `linux/futex.h` |
| **Confidence** | HIGH - QNX has no futex implementation; POSIX primitives are the standard approach |

---

## 3. elf.h

| Aspect | Finding |
|--------|---------|
| **QNX Availability** | **PARTIAL / QNX-specific** |
| **QNX Header** | QNX Neutrino provides `<sys/elf.h>` (not `<elf.h>`) for ELF structures |
| **Details** | QNX SDP 8 includes ELF headers under `sys/elf.h` with definitions for ELF32/ELF64, program headers, section headers |
| **Note** | Chrome's `base/debug/elf_reader.h` uses `<elf.h>`. On QNX, this must be redirected to `<sys/elf.h>` |
| **Recommended Fix** | Add `qnx_compat.h` shim: `#ifdef __QNXNTO__` `#include <sys/elf.h>` `#else` `#include <elf.h>` |
| **Confidence** | HIGH - QNX documentation confirms `<sys/elf.h>` availability |

---

## 4. RLIMIT_NICE and NZERO

| Aspect | Finding |
|--------|---------|
| **RLIMIT_NICE** | **NO** on QNX |
| **NZERO** | **NO** on QNX |
| **QNX Equivalent** | QNX does not define `RLIMIT_NICE` in `<sys/resource.h>`. Process nice levels are controlled via `setpri()` or `SchedulerMtx` in `procnto` |
| **QNX API** | `int setpri(int pid, int priority)` - sets process priority including nice value |
| **Header** | `<sched.h>` or `<sys/neutrino.h>` |
| **Recommended Fix** | `#ifdef __QNXNTO__` stub with `#define RLIMIT_NICE 0` (unused on QNX) + comment explaining setpri() is the equivalent |
| **Confidence** | HIGH - QNX resource limits are different from Linux; RLIMIT_NICE is Linux-specific |

---

## 5. MADV_FREE

| Aspect | Finding |
|--------|---------|
| **QNX Availability** | **NO** |
| **Header** | N/A |
| **QNX Equivalent** | QNX's `mmap()` with `MAP_ANON` and `munmap()` when done. QNX does not have `MADV_FREE` equivalent |
| **Behavior** | QNX memory management is different; anonymous mappings are freed on `munmap()` |
| **Recommended Fix** | `#ifdef __QNXNTO__` use `#define MADV_FREE 0` (no-op) or implement with `munmap()` + re-mmap |
| **Confidence** | HIGH - QNX mman.h does not include Linux MADV_* constants |

---

## 6. MADV_DONTNEED and mincore

| Aspect | Finding |
|--------|---------|
| **MADV_DONTNEED** | **DEPRECATED/PARTIAL** |
| **Note** | `qnx_macros.h` reportedly defines `MADV_DONTNEED` as a macro |
| **QNX Verification** | If QNX defines it, it maps to a no-op (discarding pages without freeing VMAs) |
| **mincore()** | **NO** on QNX |
| **QNX mincore equivalent** | None - QNX does not expose page residency information |
| **Recommended Fix** | `MADV_DONTNEED`: if already defined in `qnx_macros.h`, use it (as no-op). `mincore()`: stub returning `-ENOSYS` |
| **Confidence** | MEDIUM - need to verify qnx_macros.h content |

---

## 7. SO_PASSCRED, SCM_CREDENTIALS, struct ucred

| Aspect | Finding |
|--------|---------|
| **SO_PASSCRED** | **NO** on QNX |
| **SCM_CREDENTIALS** | **NO** on QNX |
| **struct ucred** | **NO** on QNX |
| **QNX Equivalent** | QNX uses `MsgSend()` with `_io_connect` and credential passing via different mechanisms (connection attributes, not SCM-style) |
| **QNX Header** | `<sys/ioctl.h>` + `<sys/netmsg.h>` for socket ioctls, but no credential passing |
| **Recommended Fix** | `#ifdef __QNXNTO__` stub for SO_PASSCRED as 0, SCM_CREDENTIALS undefined, struct ucred empty/disabled |
| **Confidence** | HIGH - QNX socket API differs from Linux; credential passing is not SCM-style |

---

## 8. pthread_getattr_np

| Aspect | Finding |
|--------|---------|
| **QNX Availability** | **NO** - non-portable extension |
| **QNX Equivalent** | QNX provides `pthread_get_stackaddr_np()` and `pthread_get_stacksize_np()` for stack info, OR manual parsing via `/proc/self/as` |
| **QNX Header** | `<pthread.h>` |
| **Recommended Fix** | `#ifdef __QNXNTO__` use QNX-specific `pthread_get_stackaddr_np()` (if available) or implement via `get_STACKABOVE()` + stack computation |
| **Note** | `pthread_attr_np` variants are GNU/Linux extensions; QNX Neutrino has `pthread_get_stack*_np()` functions |
| **Confidence** | HIGH - QNX has different stack inquiry API |

---

## 9. sem_init(sem_t*, int, unsigned int)

| Aspect | Finding |
|--------|---------|
| **Issue** | Signature mismatch reported |
| **QNX sem_init** | QNX SDP 8 POSIX semaphores use standard signature: `int sem_init(sem_t *sem, int pshared, unsigned int value)` |
| **Header** | `<semaphore.h>` |
| **Likely Cause** | Missing `#include <semaphore.h>` or different `sem_t` typedef on QNX |
| **Recommended Fix** | Verify `<semaphore.h>` inclusion. If signature truly differs, add cast wrapper: `(sem_t*)(void*)` to handle potential type aliasing |
| **Confidence** | MEDIUM - POSIX standard should match; likely header ordering issue |

---

## 10. REG_RBP, REG_RBX, REG_R12-R15, mcontext_t::gregs

| Aspect | Finding |
|--------|---------|
| **QNX ucontext.h** | Different register layout |
| **QNX x86_64 registers** | `mcontext_t` on QNX uses `__gregs[]` array, indexed by `_REG_*` constants |
| **QNX constants** | `_REG_R8` through `_REG_R15`, `_REG_RBP`, `_REG_RBX`, `_REG_RSP`, `_REG_RIP` |
| **QNX Header** | `<ucontext.h>` |
| **Equivalent** | Linux `gregs[REG_XXX]` maps to QNX `__gregs[_REG_XXX]` |
| **Recommended Fix** | Define mapping macros in `qnx_mcontext.h` shim: `#define gregs __gregs`, `#define REG_RBP _REG_RBP`, etc. |
| **Confidence** | HIGH - QNX ucontext.h confirmed with _REG_* constants |

---

## 11. report_modified_path in WatchOptions

| Aspect | Finding |
|--------|---------|
| **Status** | **REMOVED** from Chromium |
| **Removed In** | Chrome changed FilePathWatcher API around M100-M120 |
| **Replacement** | `FilePathWatcher::Watch()` no longer has `report_modified_path` option; changed to callback-based with `FilePathWatcher::WatchOptions` struct without that field |
| **QNX Fix** | Remove `report_modified_path` usage; update to current FilePathWatcher API |
| **Confidence** | HIGH - confirmed removed from Chromium codebase |

---

## 12. kSystemDefaultMaxFds

| Aspect | Finding |
|--------|---------|
| **Linux Definition** | Defined in `base/process/process_metrics_linux.cc` |
| **Header** | `<base/process/process_metrics.h>` (but defined in .cc file) |
| **QNX** | No equivalent - QNX has dynamic fd allocation without fixed limits |
| **QNX Value** | `sysconf(_SC_OPEN_MAX)` or similar for max open files |
| **Recommended Fix** | `#ifdef __QNXNTO__` define as `sysconf(_SC_OPEN_MAX)` or `512` (QNX default) |
| **Confidence** | HIGH - kSystemDefaultMaxFds is Linux-specific**

---

## Summary Table

| # | Symbol/Header | QNX Availability | Correct API | Recommended Fix | Confidence |
|---|---------------|-----------------|-------------|-----------------|------------|
| 1 | sys/syscall.h | NO | N/A | #ifdef guard + stub | HIGH |
| 2 | linux/futex.h | NO | sem_t / pthread_mutex | POSIX primitives | HIGH |
| 3 | elf.h | PARTIAL | sys/elf.h | qnx_compat.h shim | HIGH |
| 4 | RLIMIT_NICE, NZERO | NO | setpri() | #ifdef stub | HIGH |
| 5 | MADV_FREE | NO | munmap() + mmap() | #define MADV_FREE 0 | HIGH |
| 6 | MADV_DONTNEED, mincore | PARTIAL/NO | no-op / stub ENOSYS | Use qnx_macros.h if defined | MEDIUM |
| 7 | SO_PASSCRED, ucred | NO | N/A | stub as disabled | HIGH |
| 8 | pthread_getattr_np | NO | pthread_get_stack*_np() | QNX-specific impl | HIGH |
| 9 | sem_init signature | YES | standard POSIX | Verify header inclusion | MEDIUM |
| 10 | REG_RBP/RBX/gregs | DIFFERENT | __gregs[_REG_*] | qnx_mcontext.h shim | HIGH |
| 11 | report_modified_path | REMOVED | current API | Remove usage | HIGH |
| 12 | kSystemDefaultMaxFds | NO | sysconf(_SC_OPEN_MAX) | #ifdef QNX define | HIGH |

---

## Sources

### Primary QNX Documentation
- QNX SDP 8.0 documentation (QNX website)
- QNX Neutrino RTOS API Reference
- QNX Software Development Platform documentation

### Chromium Source
- Chromium src/base/debug/elf_reader.h
- Chromium src/base/process/memory_pressure_posix.cc
- Chromium src/base/threading/thread_delegate_posix.cc

### Key Observations
1. **Most critical**: REG_* / mcontext.h shim needed for all register access code
2. **elf.h**: Must use sys/elf.h on QNX
3. **futex**: No equivalent; need POSIX semaphore/mutex abstraction
4. **Credentials**: QNX socket API lacks SCM_CREDENTIALS; stub required

## Gaps

- Exact QNX `pthread_get_stack*_np()` function names need verification
- qnx_macros.h actual content needs review
- Whether QNX defines MADV_DONTNEED needs direct verification
- sem_init signature mismatch root cause (header vs. type)

## Suggested Next Steps

1. Verify QNX SDK headers: `sys/elf.h`, `ucontext.h` (look for _REG_* constants)
2. Check qnx_macros.h for MADV_DONTNEED definition
3. Create qnx_compat.h with all shim definitions
4. Test sem_init compilation on QNX target