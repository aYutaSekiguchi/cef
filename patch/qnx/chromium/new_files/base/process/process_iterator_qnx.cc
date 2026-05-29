// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_iterator.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/procfs.h>
#include <unistd.h>

#include <devctl.h>

namespace base {

ProcessIterator::ProcessIterator(const ProcessFilter* filter)
    : procfs_dir_(nullptr, DIRClose()), filter_(filter) {
  procfs_dir_.reset(opendir("/proc"));
}

ProcessIterator::~ProcessIterator() = default;

bool ProcessIterator::CheckForNextProcess() {
  if (!procfs_dir_)
    return false;
  while (true) {
    struct dirent* dp = readdir(procfs_dir_.get());
    if (!dp)
      return false;
    // Skip non-numeric entries.
    if (dp->d_name[0] < '0' || dp->d_name[0] > '9')
      continue;
    int pid = atoi(dp->d_name);
    if (pid <= 0)
      continue;

    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/as", pid);
    int fd = open(path, O_RDONLY);
    if (fd == -1)
      continue;
    procfs_info info;
    int ret = devctl(fd, DCMD_PROC_INFO, &info, sizeof(info), nullptr);
    close(fd);
    if (ret != EOK)
      continue;

    entry_.pid_ = pid;
    entry_.ppid_ = info.parent;
    entry_.gid_ = info.pgrp;

    // QNX debug_process_t does not carry the executable path.
    // Read /proc/<pid>/cmdline as fallback.
    char cmdpath[64];
    snprintf(cmdpath, sizeof(cmdpath), "/proc/%d/cmdline", pid);
    int cfd = open(cmdpath, O_RDONLY);
    if (cfd != -1) {
      char buf[256] = {};
      ssize_t n = read(cfd, buf, sizeof(buf) - 1);
      close(cfd);
      if (n > 0) {
        buf[n] = '\0';
        const char* base = strrchr(buf, '/');
        entry_.exe_file_ = base ? base + 1 : buf;
      }
    }
    if (entry_.exe_file_.empty())
      entry_.exe_file_ = dp->d_name;
    return true;
  }
}

bool NamedProcessIterator::IncludeEntry() {
  return entry_.exe_file_ == executable_name_;
}

}  // namespace base
