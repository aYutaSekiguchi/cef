// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/resource_coordinator/public/cpp/memory_instrumentation/os_metrics.h"

#include "base/process/process_handle.h"

namespace memory_instrumentation {

std::vector<mojom::VmRegionPtr> OSMetrics::GetProcessMemoryMaps(
    base::ProcessHandle handle) {
  return std::vector<mojom::VmRegionPtr>();
}

bool OSMetrics::FillOSMemoryDump(
    base::ProcessHandle handle,
    const MemDumpFlagSet& flags,
    mojom::RawOSMemDump* dump) {
  return false;
}

}  // namespace memory_instrumentation
