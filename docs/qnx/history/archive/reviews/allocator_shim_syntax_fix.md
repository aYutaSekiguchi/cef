# Allocator Shim Syntax Fix

## Problem
`#endif without #if` - Preprocessor directive mismatch at line 24.

### Root Cause
The original structure had:
```
#ifndef __THROW   // if __THROW not defined
  #ifdef _NOEXCEPT  // if _NOEXCEPT defined
    #define __THROW _NOEXCEPT
  #else
    #define __THROW
  #endif  // !_NOEXCEPT

  #if defined(__GNUC__) || PA_BUILDFLAG(IS_POSIX)   // LINE 23 - INSIDE #ifndef
    #define SHIM_ALWAYS_EXPORT ...
  #elif PA_BUILDFLAG(IS_WIN)                         // LINE 24 - #elif without matching #if
    #define __THROW
    #define SHIM_ALWAYS_EXPORT ...
  #endif
#endif  // !__THROW
```

The `#elif` on line 24 was inside `#ifndef __THROW` but had its own `#if/#elif/#endif` block, creating mismatched nesting.

## Fix
Moved the `#if defined(__GNUC__)` / `#elif PA_BUILDFLAG(IS_WIN)` block inside both branches of `#ifdef _NOEXCEPT` / `#else` / `#endif`:

```
#ifndef __THROW
  #ifdef _NOEXCEPT
    #define __THROW _NOEXCEPT
    #if defined(__GNUC__) || PA_BUILDFLAG(IS_POSIX)
      #define SHIM_ALWAYS_EXPORT ...
    #elif PA_BUILDFLAG(IS_WIN)
      #define SHIM_ALWAYS_EXPORT ...
    #endif
  #else
    #define __THROW
    #if defined(__GNUC__) || PA_BUILDFLAG(IS_POSIX)
      #define SHIM_ALWAYS_EXPORT ...
    #elif PA_BUILDFLAG(IS_WIN)
      #define SHIM_ALWAYS_EXPORT ...
    #endif
  #endif  // !_NOEXCEPT
#endif  // !__THROW
```

## Files Modified
- `base/allocator/partition_allocator/src/partition_alloc/shim/allocator_shim_internals.h`

## Verification
Should compile without `#endif without #if` errors.