// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

namespace base {

bool CheckPThreadStackMinIsSafe() {
  return true;
}

}  // namespace base
