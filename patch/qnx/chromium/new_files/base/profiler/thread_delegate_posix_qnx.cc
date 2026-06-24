// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/profiler/thread_delegate_posix.h"

namespace base {

std::unique_ptr<ThreadDelegatePosix> ThreadDelegatePosix::Create(
    SamplingProfilerThreadToken thread_token) {
  return nullptr;
}

}  // namespace base
