# GN `is_linux` vs C++ `BUILDFLAG(IS_LINUX)` Discrepancy — QNX Port Detailed Investigation

## 1. Nature of the Discrepancy

### GN Side (BUILDCONFIG.gn:318-322)

```python
is_qnx = current_os == "qnx"
# ...
is_linux = current_os == "linux" || is_qnx
```

In GN, `is_linux = true` (including QNX) governs build-graph construction.
Linux-specific sources in `base/BUILD.gn` are added under the `is_linux` condition, and files explicitly excluded for QNX are already listed there.

### C++ Side (build_config.h:91-115)

```c
#if !defined(OS_CHROMEOS)
#define OS_LINUX 1    // ← Determined by __linux__: QNX has __QNXNTO__ so undefined
#endif

#elif defined(__QNXNTO__)
#define OS_QNX 1      // ← QNX maps to OS_QNX, not OS_LINUX
```

```c
#define BUILDFLAG_INTERNAL_IS_LINUX() (defined(OS_LINUX) ? 1 : 0)  // = 0 on QNX
#define BUILDFLAG_INTERNAL_IS_QNX()  (defined(OS_QNX) ? 1 : 0)     // = 1 on QNX
```

**Result**: GN treats QNX as Linux and includes Linux-specific sources in the build graph, but `BUILDFLAG(IS_LINUX)` is `0` in C++, causing all `#if BUILDFLAG(IS_LINUX)` blocks to be skipped entirely.

---

## 2. Count of Files Using `BUILDFLAG(IS_LINUX)`

| Category | Count |
|---|---|
| All files under `base/` (including tests) | 137 files |
| Files under `base/` (tests excluded) | ~110 files |
| Files under `partition_alloc/` (tests excluded) | 42 files |
| **Total** | **~152 files** |

### Key Files (base/, tests excluded)

| File | Purpose | What IS_LINUX Enables |
|---|---|---|
| `i18n/icu_util.cc` | ICU timezone | `getLocalTimezone` Linux path |
| `rand_util_posix.cc` | Random number generation | `getrandom()` syscall path |
| `features.cc` | Feature flags | `MessagePumpEpoll` initialization |
| `threading/platform_thread_posix.cc` | Thread management | TLS tid cache |
| `syslog_logging.cc` | System logging | syslog mechanism |
| `base_paths_posix.cc` | Path resolution | `$XDG_CONFIG_HOME` resolution |
| `tracing/trace_time.cc` | Tracing | Time source |
| `threading/platform_thread_metrics.cc` | Metrics | Scheduler statistics |
| `allocator/allocator_check.cc` | Allocator check | `<malloc.h>` include |
| `allocator/dispatcher/tls.cc` | TLS | Thread-local storage |
| `process/*_linux.cc` (15 files) | Process operations | `/proc/` reads |

### Key partition_alloc/ Files

| File | Purpose | What IS_LINUX Enables |
|---|---|---|
| `partition_root.cc` | Allocator core | futex locks, ARM64 page-size checks |
| `spinning_mutex.h` | Spin lock | `futex()` kernel blocking |
| `page_allocator_constants.h` | Page management | `PR_SET_VMA_ANON_NAME` (Linux named VMAs) |
| `page_allocator_internals_posix.h` | Page allocation | `msseal()` call (Linux 5.17+) |
| `partition_page_constants.h` | Page-size constants | Dynamic page-size retrieval |
| `address_space_randomization.h` | ASLR | Address-space randomization |
| `cpu.cc` | CPU detection | Linux kernel version comparison |
| `stack_trace.cc` | Stack trace | glibc backtrace |
| `tagging.cc` | MTE memory tagging | `prctl(PR_SET_TAGGED_ADDR_CTRL)` |
| `thread_isolation/pkey.cc` | Memory protection keys | `syscall(SYS_pkey_mprotect)` — **BUILD ERROR** |

---

## 3. Dangerous Activations When `BUILDFLAG(IS_LINUX)=1` Is Assumed

### 3.1 Files with `#error` That Stop the Build

| File | Line | Content |
|---|---|---|
| `thread_isolation/pkey.cc` | 19 | `#error "This pkey code is currently only supported on Linux and ChromeOS"` |

On QNX, enabling `IS_LINUX=1` would trigger this `#error`, activating pkey code that calls `pkey_mprotect`.

### 3.2 Direct Dependencies on Linux-Only Syscalls

| Syscall | File | Line | Risk |
|---|---|---|---|
| `__NR_getrandom` | `rand_util_posix.cc` | 77 | QNX has `/dev/urandom` but `getrandom()` syscall is not supported |
| `syscall(SYS_pkey_mprotect)` | `thread_isolation/pkey.cc` | 28 | QNX has no `pkey_mprotect` |
| `__NR_mseal` | `page_allocator_internals_posix.h` | 349 | Linux 5.17+ only — QNX returns ENOSYS |
| `futex()` | `spinning_mutex.h` | 42, `spinning_mutex.cc` | No `futex` on QNX — fallback path required |

### 3.3 Dependencies on the `/proc/` Filesystem

| File | Line | Content |
|---|---|---|
| `partition_alloc_base/debug/proc_maps_linux.cc` | 60 | Opens `/proc/self/maps` — `/proc` does not exist on QNX |
| `base/process/process_*_linux.cc` (15 files) | Various | Reads `/proc/self/...` |
| `rand_util_posix.cc` | 58 | `/dev/urandom` — partially safe since `/dev/urandom` typically exists on QNX |

### 3.4 `prctl()` and Linux-Specific Headers

| File | Dependency | QNX Compatibility |
|---|---|---|
| `tagging.cc` | `<sys/prctl.h>`, `<linux/version.h>` | QNX lacks `PR_SET_TAGGED_ADDR_CTRL`. MTE is ARM-hardware-specific so it may work on QNX/ARM, but `prctl(PR_SET_TAGGED_ADDR_CTRL)` is Linux-specific — **risky** |
| `page_allocator_constants.h` | `<sys/prctl.h>` | QNX lacks `PR_SET_VMA_ANON_NAME` |
| `base/android/thread_instruction_count.cc` | `syscall(__NR_perf_event_open)` | QNX has no perf subsystem |

### 3.5 `__GLIBC__` Dependencies

| File | Line | Content |
|---|---|---|
| `stack_trace.cc` | 19, 243 | `(PA_BUILDFLAG(IS_LINUX) || PA_BUILDFLAG(IS_CHROMEOS)) && defined(__GLIBC__)` — QNX does not use glibc |
| `partition_alloc_config.h` | — | `PA_CONFIG_HAS_LINUX_KERNEL()` maps to `IS_LINUX || IS_CHROMEOS || IS_ANDROID` |

---

## 4. Estimated Scope of Changes for Adding `BUILDFLAG(IS_LINUX)=0` + `BUILDFLAG(IS_QNX)`

### 4.1 Changes That Require Only Simple Replacement

Sites across all IS_LINUX references (~281 + 42 = ~323 total) can be updated with a simple substitution:

```
IS_LINUX || IS_CHROMEOS    → IS_LINUX || IS_CHROMEOS || IS_QNX
IS_LINUX || IS_ANDROID    → IS_LINUX || IS_ANDROID || IS_QNX
```

This is the **safest approach** — it enables Linux-compatible behavior on QNX where QNX Neutrino's POSIX compatibility makes Linux code likely to function.

### 4.2 Sites Requiring Individual Evaluation

| File | Location | Evaluation |
|---|---|---|
| `thread_isolation/pkey.cc` | `#error !IS_LINUX && !IS_CHROMEOS` | **Remove the #error and add IS_QNX** — `mprotect`-family syscalls may work on QNX |
| `rand_util_posix.cc` | `syscall(__NR_getrandom)` | **Add fallback to `/dev/urandom` for QNX** — existing `!IS_LINUX` else branch already uses `/dev/urandom` |
| `proc_maps_linux.cc` | `/proc/self/maps` | **QNX has a different `/proc` structure** — `proc_maps_qnx.cc` already exists, so this file is not needed |
| `spinning_mutex.h` | `futex()` | **POSIX `pthread_mutex` fallback already exists** (via the `IS_POSIX` case) |
| `tagging.cc` | `prctl()`, MTE | **QNX may run on ARM** — MTE is an ARM hardware feature so it could work, but `prctl(PR_SET_TAGGED_ADDR_CTRL)` is Linux-specific |
| `page_allocator_internals_posix.h` | `msseal` | **QNX has no `msseal`** — Linux-version check prevents activation on QNX |

### 4.3 Change-Quantity Estimate

| Work Category | Scope |
|---|---|
| Adding `|| IS_QNX` (simple OR additions) | ~280 sites |
| QNX-specific replacements (`pkey.cc`, etc.) | 5–10 files |
| New QNX-specific files needed (`proc_maps_qnx.cc` already exists) | Not needed |
| **Total change sites** | **~290 sites** |

---

## 5. QNX-Specific Files Already Added in base/

Already present under `base/BUILD.gn` for QNX:

```python
if (is_qnx) {
  sources += [
    "debug/proc_maps_qnx.cc",      # QNX proc-maps implementation
    "debug/proc_maps_linux.h",     # Header reused
    "debug/stack_trace_qnx.cc",    # QNX stack-trace implementation
  ]
  libs += [ "backtrace" ]          # QNX backtrace library
}
```

---

## 6. Comparison with ChromeOS

ChromeOS maintains GN/C++ synchronization:
- GN: `is_chromeos = current_os == "chromeos"`
- C++: `OS_CHROMEOS` is defined in GN, and build_config.h uses that value

**QNX's problem: `is_linux` was extended to include QNX, but `build_config.h`'s `#elif __linux__` guard was designed to exclude QNX.** ChromeOS-style synchronization in `build_config.h` would have resolved this, but it was not done.

---

## 7. Implications for Decision-Making

### Scenario A: Set `OS_LINUX=1` so `BUILDFLAG(IS_LINUX)=1`

**High risk — not recommended.**

| Problem | Severity |
|---|---|
| QNX does not use glibc (it is musl/qcc-based) | High — `__GLIBC__`-dependent stack traces will crash |
| QNX has no `/proc/` filesystem | High — all `proc_maps_linux.cc` code fails |
| QNX has no `futex()` | High — `SpinningMutex` cannot block in the kernel |
| QNX has no `pkey_mprotect` | High — pkey code will get syscall errors |
| QNX has no `msseal` | Medium — simply returns ENOSYS |
| QNX lacks MTE-compatible `prctl` | Medium — MTE hardware may still function |
| QNX has `/dev/urandom` | Low — random numbers work via the existing path |

### Scenario B: Add `BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_QNX)` at Each Site

**Medium risk — a practical approach.**

| Task | Workload |
|---|---|
| Adding `|| IS_QNX` | ~280 replacements |
| Fixing `pkey.cc`'s `#error` | 1 file |
| `proc_maps_linux.cc` already handled by `proc_maps_qnx.cc` | Not needed |
| `spinning_mutex.h` has POSIX fallback | Conditionally safe |
| `rand_util_posix.cc` already has `/dev/urandom` else branch | Not needed (already safe) |
| `page_allocator_internals_posix.h`'s `msseal` | Protected by Linux-version check on QNX |

### Scenario C: Adding `IS_QNX` to `PA_CONFIG_HAS_LINUX_KERNEL()`

**Low risk — recommended for partition_alloc.**

```c
// Change in partition_alloc_config.h
#define PA_CONFIG_HAS_LINUX_KERNEL()                      \
  (PA_BUILDFLAG(IS_LINUX) || PA_BUILDFLAG(IS_CHROMEOS) || \
   PA_BUILDFLAG(IS_ANDROID) || PA_BUILDFLAG(IS_QNX))
```

QNX Neutrino is not a Linux kernel, but its high POSIX compatibility means many Linux-style APIs work. Since futex-based operations already have POSIX `pthread_mutex` alternatives on QNX, this change enables partition_alloc core functionality.

### Recommended Actions

| Priority | Action | Reason |
|---|---|---|
| **Immediate** | Fix `pkey.cc` to `!IS_LINUX && !IS_CHROMEOS && !IS_QNX` | Prevent `#error` from stopping the build |
| **High** | In `rand_util_posix.cc`, enable the `getrandom` path with `|| IS_QNX` (safe because `/dev/urandom` exists on QNX) | Guarantees correct behavior |
| **High** | Add `|| IS_QNX` to `page_allocator_constants.h` and `page_allocator_internals_posix.h` | QNX has a `/proc/self/maps`-like procfs |
| **Medium** | Add `IS_QNX` to `PA_CONFIG_HAS_LINUX_KERNEL()` in `spinning_mutex.h` | POSIX `pthread_mutex` fallback already exists |
| **Medium** | `stack_trace.cc`'s `__GLIBC__` dependency is not needed on QNX (already handled by `stack_trace_qnx.cc`) | QNX path already exists in GN |
| **Low** | `tagging.cc`'s `prctl` calls | Only needed if QNX ARM uses MTE |

---

## Appendix: Risk Matrix by File

| File | IS_LINUX Sites | QNX Risk | Recommended Action |
|---|---|---|---|
| `thread_isolation/pkey.cc` | 1 (`#error`) | Build error | Fix required |
| `spinning_mutex.h/.cc` | Several | No futex → pthread fallback | Change `PA_CONFIG_HAS_LINUX_KERNEL()` |
| `page_allocator_constants.h` | ~8 | No PR_SET_VMA | Add `|| IS_QNX` |
| `page_allocator_internals_posix.h` | ~3 | No msseal | Protected by Linux-version check |
| `proc_maps_linux.cc` | ~5 | No /proc | Not needed (`proc_maps_qnx.cc` provides alternate definition) |
| `rand_util_posix.cc` | ~5 | No getrandom → urandom path | Add `|| IS_QNX` |
| `partition_root.cc` | ~5 | futex dependency | Change `PA_CONFIG_HAS_LINUX_KERNEL()` |
| `tagging.cc` | ~2 | No prctl | Add `|| IS_QNX` (QNX ARM only) |
| `stack_trace.cc` | ~2 | glibc dependency | Replaced by existing qnx file |
| `cpu.cc` | ~3 | Version checks | Add `|| IS_QNX` |
| `partition_page_constants.h` | ~1 | Dynamic page size | Add `|| IS_QNX` |
| `address_space_randomization.h` | ~2 | Linux randoms | Add `|| IS_QNX` |
| `i18n/icu_util.cc` | ~2 | Timezone | Add `|| IS_QNX` |
| `features.cc` | ~2 | epoll | Add `|| IS_QNX` |
| `syslog_logging.cc` | ~2 | syslog | Add `|| IS_QNX` |
| `base_paths_posix.cc` | ~1 | XDG paths | Add `|| IS_QNX` |
| `tracing/trace_time.cc` | ~1 | Time source | Add `|| IS_QNX` |
| `allocator_check.cc` | ~1 | malloc.h | Add `|| IS_QNX` |
| `threading/platform_thread_*.cc` | ~6 | tid cache | Add `|| IS_QNX` |