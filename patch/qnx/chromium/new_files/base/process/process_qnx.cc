// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process.h"

#include <devctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/resource.h>

#include <optional>

#include <sys/procfs.h>

#include "base/check.h"
#include "base/files/scoped_file.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"

namespace base {
namespace {

std::optional<procfs_info> GetProcInfo(ProcessId pid) {
  char path[64];
  const int len =
      snprintf(path, sizeof(path), "/proc/%d/as", static_cast<int>(pid));
  if (len < 0 || static_cast<size_t>(len) >= sizeof(path)) {
    return std::nullopt;
  }

  ScopedFD fd(open(path, O_RDONLY));
  if (!fd.is_valid()) {
    return std::nullopt;
  }

  procfs_info info = {};
  const int status = devctl(fd.get(), DCMD_PROC_INFO, &info, sizeof(info),
                            nullptr);
  if (status != EOK) {
    return std::nullopt;
  }

  return info;
}

}  // namespace

ProcessId GetParentProcessId(ProcessHandle handle) {
  char path[64];
  snprintf(path, sizeof(path), "/proc/%d/as", handle);
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    return -1;
  procfs_info info;
  int ret = devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), nullptr);
  close(fd);
  if (ret != EOK)
    return -1;
  return info.parent;
}

Time Process::CreationTime() const {
  DCHECK(IsValid());

  ScopedAllowBlocking scoped_allow_blocking;

  std::optional<procfs_info> info = GetProcInfo(Pid());
  if (!info || info->start_time == 0) {
    return Time();
  }

  // QNX procfs reports process start_time as nanoseconds since the Unix epoch
  // (not nanoseconds since boot), so do not add qtime.boot_time here.
  return Time::UnixEpoch() + Nanoseconds(info->start_time);
}

namespace {

constexpr int kForegroundPriority = 0;
constexpr int kBackgroundPriority = 5;

}  // namespace

// static
bool Process::CanSetPriority() {
  // QNX's setpriority(PRIO_PROCESS, pid, ...) does not provide the Linux nice
  // semantics Chromium's process priority API expects, and GetPriority() can't
  // reliably observe BestEffort/UserBlocking transitions. Report unsupported
  // rather than claiming support and failing callers/tests.
  return false;
}

Process::Priority Process::GetPriority() const {
  DCHECK(IsValid());
  return GetOSPriority() == kBackgroundPriority
             ? Priority::kBestEffort
             : Priority::kUserBlocking;
}

bool Process::SetPriority(Priority priority) {
  DCHECK(IsValid());

  if (!CanSetPriority()) {
    return false;
  }

  const int priority_value =
      priority == Priority::kBestEffort ? kBackgroundPriority
                                        : kForegroundPriority;

  const int result = setpriority(PRIO_PROCESS,
                                 static_cast<id_t>(process_), priority_value);
  DPCHECK(result == 0);
  return result == 0;
}

}  // namespace base
