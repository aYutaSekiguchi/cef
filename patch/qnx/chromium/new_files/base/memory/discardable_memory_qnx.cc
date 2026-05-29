// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/memory/discardable_memory.h"

#include "base/memory/madv_free_discardable_memory_posix.h"

namespace base {

MadvFreeSupport GetMadvFreeSupport() {
  // QNX does not support MADV_FREE.
  return MadvFreeSupport::kUnsupported;
}

}  // namespace base
