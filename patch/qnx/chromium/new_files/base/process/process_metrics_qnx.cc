// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <fcntl.h>
#include <devctl.h>
#include <sys/procfs.h>
#include <unistd.h>

#include "base/files/scoped_file.h"
#include "base/memory/ptr_util.h"
#include "base/types/expected.h"

namespace base {

ProcessMetrics::ProcessMetrics(ProcessHandle process) : process_(process) {}

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  return WrapUnique(new ProcessMetrics(process));
}

size_t GetSystemCommitCharge() {
  // QNX does not have a concept of virtual memory overcommit.
  return 0;
}

base::expected<TimeDelta, ProcessCPUUsageError>
ProcessMetrics::GetCumulativeCPUUsage() {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/as", process_);
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  procfs_info info;
  int ret = devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), nullptr);
  close(fd);
  if (ret != EOK)
    return base::unexpected(ProcessCPUUsageError::kSystemError);
  // utime and stime are in nanoseconds.
  return Nanoseconds(info.utime + info.stime);
}

base::expected<ProcessMemoryInfo, ProcessUsageError>
ProcessMetrics::GetMemoryInfo() const {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/as", process_);
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    return base::unexpected(ProcessUsageError::kSystemError);
  procfs_info info;
  int ret = devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), nullptr);
  close(fd);
  if (ret != EOK)
    return base::unexpected(ProcessUsageError::kSystemError);
  ProcessMemoryInfo memory_info;
  memory_info.resident_set_bytes = info.private_mem;
  return memory_info;
}

}  // namespace base
