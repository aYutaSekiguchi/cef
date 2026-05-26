# Allocator Shim `__THROW` Error on QNX

## Error
```
error: exception specification in declaration does not match previous declaration
   SHIM_ALWAYS_EXPORT void* calloc(size_t n, size_t size) __THROW {
```

## QNX malloc.h Function Signatures

QNX `malloc.h` does **NOT** include `__THROW` on any memory allocation functions:

```c
extern void *calloc(size_t __n, size_t __size)
        __attribute__((__alloc_size__(1, 2)));
extern void *malloc(size_t __size) __attribute__((__alloc_size__(1)));
extern void *realloc(void *__ptr, size_t __size)
        __attribute__((__alloc_size__(2)));
extern void free(void *__ptr);
extern void *aligned_alloc(size_t __alignment, size_t __size);
extern int posix_memalign(void **__memptr, size_t __alignment, size_t __size);
extern void *valloc(size_t __size);
```

## `__THROW` Macro Definition Status

**QNX `sys/cdefs.h` does NOT define `__THROW`.**

```
grep -rn "__THROW" ~/qnx800/target/qnx/usr/include/ -> no matches
```

## Root Cause

In `allocator_shim_internals.h` (lines 13-20):

```c
#if PA_BUILDFLAG(IS_POSIX)
#include <sys/cdefs.h>  // for __THROW
#endif

#ifndef __THROW   // Not a glibc system
#ifdef _NOEXCEPT  // LLVM libc++ uses noexcept instead
#define __THROW _NOEXCEPT
#else
#define __THROW
#endif  // !_NOEXCEPT
#endif
```

**Problem chain:**
1. QNX includes `<sys/cdefs.h>` but it doesn't define `__THROW`
2. `#ifndef __THROW` is true, falls through to `#ifdef _NOEXCEPT`
3. If `_NOEXCEPT` is defined on QNX toolchain, `__THROW` becomes `_NOEXCEPT`
4. Chromium's shim functions use `__THROW`, but QNX `malloc.h` declares functions **without** exception specifications
5. Result: exception specification mismatch error

## Recommended Fix

Add QNX-specific define in `allocator_shim_internals.h`:

```c
// allocator_shim_internals.h

#if defined(__GNUC__)

#if defined(PA_IS_QNX)
#define __THROW
#elif PA_BUILDFLAG(IS_POSIX)
#include <sys/cdefs.h>  // for __THROW
#ifndef __THROW   // Not a glibc system
#ifdef _NOEXCEPT  // LLVM libc++ uses noexcept instead
#define __THROW _NOEXCEPT
#else
#define __THROW
#endif  // !_NOEXCEPT
#endif
#endif  // IS_POSIX
```

## Files to Modify

| File | Lines | Change |
|------|-------|--------|
| `base/allocator/partition_allocator/src/partition_alloc/shim/allocator_shim_internals.h` | 11-21 | Add QNX conditional before POSIX `#include <sys/cdefs.h>` |

## Start Here

**File:** `base/allocator/partition_allocator/src/partition_alloc/shim/allocator_shim_internals.h`

**Why:** Contains the `__THROW` macro definition logic. Modify here to force empty `__THROW` for QNX before attempting to include system headers.

## Priority

**High** - Compilation fails on QNX target.