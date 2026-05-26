# QNX `proc_maps_linux` Investigation Report

## Investigation Results

### QNX Memory Map API

QNX does **not** have `/proc/self/maps`. Instead uses `procfs` API:

| Item | Linux | QNX |
|------|-------|-----|
| File path | `/proc/<pid>/maps` | `/proc/<pid>/as` |
| Access method | `open()`/`read()` | `devctl()` |
| Command | None (file read) | `DCMD_PROC_MAPINFO` |
| Data structure | Line parse | `procfs_mapinfo` |

**Reference**: `<QNX_SDP_ROOT>/target/qnx/usr/include/sys/procfs.h` (lines 76-91, 174-176)

### procfs_mapinfo Structure

```c
typedef struct _procfs_map_info {
    _Uint64t    vaddr;     // virtual address
    _Uint64t    size;      // size
    _Uint32t    flags;     // MAP_* flags (PROT_READ|PROT_WRITE|PROT_EXEC|MAP_PRIVATE|SHARED)
    dev_t       dev;       // device number
    off_t       offset;    // file offset (64-bit)
    ino_t       ino;       // inode (64-bit)
    _Uint64t    paddr;     // physical address
} procfs_mapinfo;
```

### SCNxPTR Macro

**SCNxPTR is defined in QNX headers**:
- `devs/include_aarch64/machine/_inttypes.h:213`
- `devs/include_x86_64/x86/_inttypes.h:223`
- `inttypes.h:265`

```c
#define SCNxPTR  "lx"  // uintptr_t scanf format
```

**Problem**: Chromium's QNX build may not be including these headers in the right order.

## File List

1. `<QNX_SDP_ROOT>/target/qnx/usr/include/sys/procfs.h` - QNX procfs API definition
2. `<QNX_SDP_ROOT>/target/qnx/usr/include/sys/debug.h` - debug structure definition
3. `<CHROMIUM_SRC_ROOT>/base/debug/proc_maps_linux.cc` - Target file (lines 1-80, 81-175)
4. `<CHROMIUM_SRC_ROOT>/base/debug/proc_maps_linux.h` - Header

## Approaches for QNX Implementation

### Approach A: QNX-Specific Implementation (Recommended)

Implement QNX version in `proc_maps_linux.cc` via `#if BUILDFLAG(IS_QNX)`:

```cpp
#if BUILDFLAG(IS_QNX)
#include <sys/procfs.h>
#include <sys/procfs.h>

bool ReadProcMaps(std::string* proc_maps) {
  // QNX: open /proc/self/as with devctl()
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/as", getpid());
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;

  // procfs_mapinfo is variable-length; get size first
  int num_maps = 0;
  devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), &num_maps);

  // Get MAPINFO
  procfs_mapinfo* maps = new procfs_mapinfo[num_maps];
  devctl(fd, DCMD_PROC_MAPINFO, maps, sizeof(procfs_mapinfo) * num_maps, &num_maps);

  // Convert format and store in proc_maps (similar to Linux /proc/self/maps format)
  // ...

  close(fd);
  delete[] maps;
  return true;
}
#endif
```

### Approach B: Stub

Stub `ReadProcMaps()` on QNX:

```cpp
#if BUILDFLAG(IS_QNX)
bool ReadProcMaps(std::string* proc_maps) {
  proc_maps->clear();
  return true;  // Not supported on QNX
}
#endif
```

### Approach C: Force inttypes.h Include

Explicitly add include in `proc_maps_linux.cc`:

```cpp
#if BUILDFLAG(IS_QNX)
#include <inttypes.h>
#endif
```

## Recommended Fix Strategy

1. **Immediate fix**: Add `#if BUILDFLAG(IS_QNX)` guard to `proc_maps_linux.cc`
2. **SCNxPTR issue**: Verify `inttypes.h` is included in QNX build
3. **Long-term fix**: Implement QNX version via Approach A if time permits

**Risk**: `ReadProcMaps()` is widely used by HeapChecker and others; stubbing may cause crashes. Implement on QNX as needed.