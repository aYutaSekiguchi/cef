# page_allocator_internals_posix.h QNX Fix

## Fix Content

### Problem 1: `madvise` Undefined
On QNX, `madvise()` doesn't exist; `posix_madvise()` is provided instead.

### Problem 2: `MAP_ANON` / `MAP_ANONYMOUS`
QNX's `sys/mman.h` already defines both directly (`MAP_ANONYMOUS = MAP_ANON`).
The existing `#ifndef MAP_ANONYMOUS` block's MAP_ANONYMOUS → MAP_ANON mapping is fine.

---

## Fixed Files

`base/allocator/partition_allocator/src/partition_alloc/page_allocator_internals_posix.h`

### Fix Location (~line 35, right after `#include <sys/mman.h>`)

```cpp
#include <sys/mman.h>
#include <sys/syscall.h>

// QNX: madvise() is not available, use posix_madvise() instead.
#if defined(__QNX__)
#define madvise posix_madvise
#endif
```

---

## Things Left Unchanged

| Location | Problem | Conclusion |
|----------|---------|------------|
| Lines 82, 265 | `MAP_ANON` | Both `MAP_ANONYMOUS` and `MAP_ANON` are directly defined on QNX |
| Line 342 | `madvise()` | Added define to alias to `posix_madvise()` |

## Verification Command

```bash
# Build verification on QNX
cd $QNX800/src
make -j$(nproc) 2>&1 | grep -E "MAP_ANON|madvise|error"
```