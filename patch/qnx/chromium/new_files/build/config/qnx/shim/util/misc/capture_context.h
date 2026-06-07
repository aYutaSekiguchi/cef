// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BUILD_CONFIG_QNX_SHIM_UTIL_MISC_CAPTURE_CONTEXT_H_
#define BUILD_CONFIG_QNX_SHIM_UTIL_MISC_CAPTURE_CONTEXT_H_

#include <ucontext.h>

namespace crashpad {
using NativeCPUContext = ucontext_t;

inline void CaptureContext(NativeCPUContext* cpu_context) {
  (void)cpu_context;
}
}  // namespace crashpad

#endif  // BUILD_CONFIG_QNX_SHIM_UTIL_MISC_CAPTURE_CONTEXT_H_
