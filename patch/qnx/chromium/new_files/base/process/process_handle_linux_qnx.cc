// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_handle.h"

#include "base/files/file_path.h"

namespace base {

FilePath GetProcessExecutablePath(ProcessHandle process) {
  return FilePath();
}

}  // namespace base
