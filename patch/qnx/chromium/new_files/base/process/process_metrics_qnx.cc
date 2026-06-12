// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_metrics.h"

#include <fcntl.h>
#include <sys/resource.h>
#include <devctl.h>
#include <sys/procfs.h>
#include <unistd.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/files/scoped_file.h"
#include "base/memory/ptr_util.h"
#include "base/process/launch.h"
#include "base/types/expected.h"

namespace base {

ProcessMetrics::ProcessMetrics(ProcessHandle process) : process_(process) {}

// static
std::unique_ptr<ProcessMetrics> ProcessMetrics::CreateProcessMetrics(
    ProcessHandle process) {
  return WrapUnique(new ProcessMetrics(process));
}

namespace {

int CountPidinFdEntries(ProcessHandle process) {
  std::string output;
  const std::vector<std::string> argv = {
      "pidin",
      "-p",
      std::to_string(static_cast<int>(process)),
      "fds",
  };
  if (!GetAppOutput(argv, &output)) {
    return -1;
  }

  int count = 0;
  size_t pos = 0;
  while (pos < output.size()) {
    size_t end = output.find('\n', pos);
    if (end == std::string::npos) {
      end = output.size();
    }
    std::string_view line(output.data() + pos, end - pos);
    pos = end + 1;

    size_t start = line.find_first_not_of(" \t\r");
    if (start == std::string_view::npos) {
      continue;
    }
    if (line[start] >= '0' && line[start] <= '9') {
      ++count;
    }
  }
  return count;
}

}  // namespace

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

int ProcessMetrics::GetOpenFdCount() const {
  return CountPidinFdEntries(process_);
}

int ProcessMetrics::GetOpenFdSoftLimit() const {
  struct rlimit limit;
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
    return -1;
  }
  return static_cast<int>(limit.rlim_cur);
}

}  // namespace base
