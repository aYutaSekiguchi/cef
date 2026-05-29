// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread_metrics.h"

#include <devctl.h>
#include <fcntl.h>
#include <sys/debug.h>
#include <sys/procfs.h>
#include <unistd.h>

#include <optional>
#include <string>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"

namespace base {

// static
std::unique_ptr<PlatformThreadMetrics>
PlatformThreadMetrics::CreateForCurrentThread() {
  return CreateFromId(PlatformThread::CurrentId());
}

// static
std::unique_ptr<PlatformThreadMetrics> PlatformThreadMetrics::CreateFromId(
    PlatformThreadId tid) {
  if (tid == kInvalidThreadId) {
    return nullptr;
  }
  return WrapUnique(new PlatformThreadMetrics(tid));
}

std::optional<TimeDelta> PlatformThreadMetrics::GetCumulativeCPUUsage() {
  TRACE_EVENT("base", "Thread::GetCumulativeCPUUsage");

  // Open /proc/<pid>/as to query per-thread status via devctl.
  const std::string path =
      "/proc/" + NumberToString(getpid()) + "/as";
  const int fd = open(path.c_str(), O_RDONLY);
  if (fd == -1) {
    DPLOG(ERROR) << "open(" << path << ") failed";
    return std::nullopt;
  }

  // DCMD_PROC_TIDSTATUS accepts a procfs_status (= debug_thread_t) with
  // tid pre-filled; the kernel fills in the rest including sutime.
  procfs_status status = {};
  status.tid = static_cast<pthread_t>(tid_.raw());

  const int rc = devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status),
                        nullptr);
  close(fd);

  if (rc != EOK) {
    DPLOG(ERROR) << "devctl(DCMD_PROC_TIDSTATUS) failed for tid "
                 << tid_.raw();
    return std::nullopt;
  }

  // sutime is thread system + user running time in nanoseconds.
  return Nanoseconds(static_cast<int64_t>(status.sutime));
}

}  // namespace base
