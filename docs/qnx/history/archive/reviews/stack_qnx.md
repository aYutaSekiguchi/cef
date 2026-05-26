# QNX Support for stack.cc

## File Modified
`base/allocator/partition_allocator/src/partition_alloc/stack/stack.cc`

## Changes Made

Added QNX-specific `GetStackTop()` implementation as `#elif defined(__QNX__)` branch (lines 44-92).

### Implementation Details

- Uses QNX `devctl` API via `/proc/self/as` to enumerate memory segments
- Opens `/proc/<pid>/as` to access address space information
- Uses `DCMD_PROC_INFO` to get segment count
- Uses `DCMD_PROC_MAPINFO` to retrieve memory map
- Finds segment containing current stack pointer via `__builtin_frame_address(0)`
- Returns highest address of matching segment (stack grows down)

### Includes Added
```cpp
#include <sys/procfs.h>
#include <fcntl.h>
#include <unistd.h>
```

### Key Differences from POSIX
- QNX lacks `pthread_getattr_np()` - uses `/proc` interface instead
- Checks `status != EOK` for devctl errors (QNX convention)
- Uses explicit casts for pointer arithmetic with `uintptr_t`