# QNX `pthread_getattr_np` Equivalent Feature Investigation

## Investigation Results

### 1. QNX pthread.h Status

**Conclusion: QNX (Neutrino) does not have `pthread_getattr_np`.**

After reviewing QNX 8.0's pthread.h (`~/qnx800/target/qnx/usr/include/pthread.h`):
- `pthread_getattr_np` declaration **not found**
- No `pthread_attr_get_np()` like FreeBSD/Solaris either
- Only attr-object-based APIs available for thread attributes

```c
// Available stack retrieval APIs (attr object only):
pthread_attr_getstack()      // line 167
pthread_attr_getstackaddr()  // line 168
pthread_attr_getstacksize()  // line 169
pthread_attr_getdefaultstacksize()  // line 192 (QNX extension)
```

### 2. Alternative Ways to Get Stack Info on QNX

On QNX, ** pthread is set at creation time via attr, so there is no API to retrieve the current thread's attributes**.

| Method | Summary | Evaluation |
|--------|---------|------------|
| `pthread_attr_getdefaultstacksize()` | Get default stack size | Main thread only |
| Read `/proc/self/as` and find `[stack]` map | Derive stack address range from `/proc/self/maps` | Recommended |
| `getrlimit(RLIMIT_STACK, ...)` | Get process stack limit | Size only, no address |

### 3. `pthread_getattr_np` Usage in Chromium

| File | Line | Purpose |
|------|------|---------|
| `v8/src/base/platform/platform-posix.cc` | 1457 | Stack::ObtainCurrentThreadStackStart() |
| `v8/src/sandbox/testing.cc` | 1044 | Stack attribute retrieval |
| `third_party/blink/renderer/platform/wtf/stack_util.cc` | 48, 117 | GetUnderestimatedStackSize(), GetStackStartImpl() |
| `base/debug/stack_trace.cc` | 226 | Stack address retrieval |
| `base/profiler/stack_base_address_posix.cc` | 67 | Same |
| `third_party/compiler-rt/.../sanitizer_linux_libcdep.cpp` | 184 | GetThreadStackTopAndBottom() |

### 4. Precedent on FreeBSD/Solaris

FreeBSD/Solaris don't have `pthread_getattr_np` either, using `pthread_attr_get_np()`:

```c
// v8/src/base/platform/platform-freebsd.cc:109-118
pthread_attr_t attr;
pthread_attr_init(&attr);
error = pthread_attr_get_np(pthread_self(), &attr);  // FreeBSD/Solaris
if (!error) {
  void* base;
  size_t size;
  error = pthread_attr_getstack(&attr, &base, &size);
  ...
}
```

### 5. Fix Strategy

#### Strategy A: Create Stack Retrieval Function in QNX Platform File (Recommended)

```cpp
// Add to v8/src/base/platform/platform-qnx.cc

#include <procfs.h>  // for procfs_mapinfo

namespace v8 {
namespace base {

// static
Stack::StackSlot Stack::ObtainCurrentThreadStackStart() {
  // QNX has no pthread_getattr_np; derive stack map from /proc/self/as
  int proc_fd;
  char buf[PATH_MAX];
  snprintf(buf, sizeof(buf), "/proc/%d/as", getpid());
  proc_fd = open(buf, O_RDONLY);
  if (proc_fd == -1) return nullptr;

  procfs_mapinfo *mapinfos = nullptr;
  int num = 0;
  if (devctl(proc_fd, DCMD_PROC_MAPINFO, nullptr, 0, &num) != EOK) {
    close(proc_fd);
    return nullptr;
  }

  mapinfos = reinterpret_cast<procfs_mapinfo*>(malloc(num * sizeof(procfs_mapinfo)));
  if (!mapinfos) {
    close(proc_fd);
    return nullptr;
  }

  if (devctl(proc_fd, DCMD_PROC_PAGEDATA, mapinfos,
             num * sizeof(procfs_mapinfo), &num) != EOK) {
    free(mapinfos);
    close(proc_fd);
    return nullptr;
  }

  void* stack_start = nullptr;
  // Identify stack map using current stack pointer
  void* current_sp = __builtin_frame_address(0);
  for (int i = 0; i < num; i++) {
    if ((uintptr_t)mapinfos[i].vaddr <= (uintptr_t)current_sp &&
        (uintptr_t)current_sp < (uintptr_t)mapinfos[i].vaddr + mapinfos[i].size) {
      // Stack map found: top = vaddr + size
      stack_start = reinterpret_cast<void*>(
          mapinfos[i].vaddr + mapinfos[i].size);
      break;
    }
  }

  free(mapinfos);
  close(proc_fd);
  return stack_start;
}

}  // namespace base
}  // namespace v8
```

#### Strategy B: Add QNX Conditional in platform-posix.cc

```cpp
// v8/src/base/platform/platform-posix.cc (~line 1457)

#if defined(V8_OS_QNX)
  // QNX: pthread_getattr_np doesn't exist. Use default values.
  // Use pthread_attr_getdefaultstacksize() on QNX
  void* base = nullptr;
  size_t size = pthread_attr_getdefaultstacksize();
  // Only accurate for main thread
#else
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  int error = pthread_getattr_np(pthread_self(), &attr);
  ...
#endif
```

#### Strategy C: Fix blink stack_util.cc

```cpp
// third_party/blink/renderer/platform/wtf/stack_util.cc

#if BUILDFLAG(IS_QNX)
  // QNX has no pthread_getattr_np
  // Use pthread_attr_getdefaultstacksize()
  return pthread_attr_getdefaultstacksize();
#elif BUILDFLAG(IS_LINUX) || BUILDFLAG(IS_CHROMEOS) || \
    BUILDFLAG(IS_ANDROID) || BUILDFLAG(IS_FREEBSD) || BUILDFLAG(IS_FUCHSIA)
  error = pthread_getattr_np(pthread_self(), &attr);
  ...
#endif
```

## Recommended Actions

1. Implement stack retrieval function in **v8/src/base/platform/platform-qnx.cc** (Strategy A)
2. Add `#elif BUILDFLAG(IS_QNX)` condition to **third_party/blink/renderer/platform/wtf/stack_util.cc**
3. Review and similarly fix **base/debug/stack_trace.cc** and **base/profiler/stack_base_address_posix.cc**

## References

- QNX pthread.h: `~/qnx800/target/qnx/usr/include/pthread.h` (lines 167-169, 192)
- FreeBSD/Solaris implementation: `v8/src/base/platform/platform-freebsd.cc:109-118`
- V8 Platform QNX: `v8/src/base/platform/platform-qnx.cc`