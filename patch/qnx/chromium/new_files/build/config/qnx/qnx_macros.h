// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license
// found in the LICENSE file.

#ifndef BUILD_CONFIG_QNX_QNX_MACROS_H_
#define BUILD_CONFIG_QNX_QNX_MACROS_H_

// QNX does not provide <link.h>, which on Linux defines the ElfW(type) macro.
// We define it here using the QNX sysroot's <sys/elf.h>.
#if defined(__QNX__)
#include <sys/elf.h>

#ifndef ElfW
#  ifdef __LP64__
#    define ElfW(type) Elf64_##type
#  else
#    define ElfW(type) Elf32_##type
#  endif
#endif

// QNX malloc.h does not use exception specifications like __THROW.
// Define it as empty to avoid mismatch errors in allocator_shim.
#define __THROW

// QNX libc++ expects operator new/delete to have noexcept.
#if __cplusplus >= 201103L
#define CXX_THROW noexcept
#else
#define CXX_THROW
#endif

// QNX does not define MAP_ANON when _POSIX_C_SOURCE is set.
// Define MAP_ANONYMOUS directly.
#if !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS 0x00080000
#endif

// QNX mallopt() uses intptr_t for the second argument.
#define QNX_MALLOPT_ARG_TYPE intptr_t

// QNX does not define madvise(), use posix_madvise() instead.
#if !defined(madvise)
#define madvise posix_madvise
#endif

// QNX does not define MADV_DONTNEED, use POSIX_MADV_DONTNEED instead.
#if !defined(MADV_DONTNEED)
#define MADV_DONTNEED POSIX_MADV_DONTNEED
#endif

// QNX does not support SA_RESTART (commented out in signal.h).
// Define it as 0 so that code which or-ies it into sa_flags compiles.
#if !defined(SA_RESTART)
#define SA_RESTART 0
#endif

#endif  // __QNX__

// Fallback for non-QNX platforms.
#if !defined(QNX_MALLOPT_ARG_TYPE)
#define QNX_MALLOPT_ARG_TYPE int
#endif

#endif  // BUILD_CONFIG_QNX_QNX_MACROS_H_
