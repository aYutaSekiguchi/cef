// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/memory.h"

#include <stdlib.h>

#include "partition_alloc/buildflags.h"

#if PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
#include "partition_alloc/shim/allocator_shim.h"  // nogncheck
#endif

namespace base {

void EnableTerminationOnHeapCorruption() {
  // QNX has no equivalent of Linux's heap corruption detection.
}

void EnableTerminationOnOutOfMemory() {
  // QNX has no OOM killer; malloc returns NULL on failure.
}

bool UncheckedMalloc(size_t size, void** result) {
#if PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
  *result = allocator_shim::UncheckedAlloc(size);
#else
  *result = malloc(size);
#endif
  return *result != nullptr;
}

bool UncheckedCalloc(size_t num, size_t size, void** result) {
#if PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
  *result = allocator_shim::UncheckedCalloc(num, size);
#else
  *result = calloc(num, size);
#endif
  return *result != nullptr;
}

void UncheckedFree(void* ptr) {
#if PA_BUILDFLAG(USE_ALLOCATOR_SHIM)
  allocator_shim::UncheckedFree(ptr);
#else
  free(ptr);
#endif
}

}  // namespace base
