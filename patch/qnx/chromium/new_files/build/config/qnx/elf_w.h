// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license
// found in the LICENSE file.

#ifndef BUILD_CONFIG_QNX_ELF_W_H_
#define BUILD_CONFIG_QNX_ELF_W_H_

// QNX does not provide <link.h>, which on Linux defines the ElfW(type) macro.
// We define it here using the QNX sysroot's <sys/elf.h> which provides the
// underlying Elf32_* / Elf64_* types.

#include <sys/elf.h>

#ifndef ElfW
#  ifdef __LP64__
#    define ElfW(type) Elf64_##type
#  else
#    define ElfW(type) Elf32_##type
#  endif
#endif

#endif  // BUILD_CONFIG_QNX_ELF_W_H_
