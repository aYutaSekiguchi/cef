// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// QNX SDP 8 implementation of base::Time / base::TimeTicks.
//
// QNX provides CLOCK_MONOTONIC (value 2, confirmed in <time.h>) but not the
// Linux-specific CLOCK_MONOTONIC_COARSE.  For low-resolution ticks we fall
// back to CLOCK_MONOTONIC; on QNX the monotonic clock is already backed by a
// high-resolution hardware timer, so there is no meaningful difference.
//
// This file replaces time_now_posix.cc for QNX builds.  It is added to the
// build in base/BUILD.gn under the QNX-specific section.

#include <stdint.h>
#include <devctl.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/debug.h>
#include <sys/procfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <optional>

#include "base/check.h"
#include "base/numerics/safe_math.h"
#include "base/time/time.h"
#include "base/time/time_override.h"

namespace {

int64_t ConvertTimespecToMicros(const struct timespec& ts) {
  if (sizeof(ts.tv_sec) <= 4 && sizeof(ts.tv_nsec) <= 8) {
    int64_t result = ts.tv_sec;
    result *= base::Time::kMicrosecondsPerSecond;
    result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
    return result;
  }
  base::CheckedNumeric<int64_t> result(ts.tv_sec);
  result *= base::Time::kMicrosecondsPerSecond;
  result += (ts.tv_nsec / base::Time::kNanosecondsPerMicrosecond);
  return result.ValueOrDie();
}

int64_t ClockNow(clockid_t clk_id) {
  struct timespec ts;
  CHECK(clock_gettime(clk_id, &ts) == 0);
  return ConvertTimespecToMicros(ts);
}

std::optional<int64_t> MaybeClockNow(clockid_t clk_id) {
  struct timespec ts;
  if (clock_gettime(clk_id, &ts) == 0) {
    return ConvertTimespecToMicros(ts);
  }
  return std::nullopt;
}

}  // namespace

namespace base {

// Time -----------------------------------------------------------------------

namespace subtle {

Time TimeNowIgnoringOverride() {
  struct timeval tv;
  struct timezone tz = {0, 0};  // UTC
  CHECK(gettimeofday(&tv, &tz) == 0);
  return Time() +
         Microseconds((tv.tv_sec * Time::kMicrosecondsPerSecond + tv.tv_usec) +
                      Time::kTimeTToMicrosecondsOffset);
}

Time TimeNowFromSystemTimeIgnoringOverride() {
  return TimeNowIgnoringOverride();
}

}  // namespace subtle

// TimeTicks ------------------------------------------------------------------

namespace subtle {

TimeTicks TimeTicksNowIgnoringOverride() {
  return TimeTicks() + Microseconds(ClockNow(CLOCK_MONOTONIC));
}

std::optional<TimeTicks> MaybeTimeTicksNowIgnoringOverride() {
  std::optional<int64_t> now = MaybeClockNow(CLOCK_MONOTONIC);
  if (now.has_value()) {
    return TimeTicks() + Microseconds(now.value());
  }
  return std::nullopt;
}

// QNX does not provide CLOCK_MONOTONIC_COARSE (a Linux-specific low-resolution
// monotonic clock).  CLOCK_MONOTONIC is already high-resolution on QNX and
// serves as an adequate substitute.
TimeTicks TimeTicksLowResolutionNowIgnoringOverride() {
  return TimeTicks() + Microseconds(ClockNow(CLOCK_MONOTONIC));
}

}  // namespace subtle

// ThreadTicks ----------------------------------------------------------------

namespace subtle {

ThreadTicks ThreadTicksNowIgnoringOverride() {
  // QNX does not provide CLOCK_THREAD_CPUTIME_ID, but procfs exposes per-thread
  // cumulative CPU time (user + system) via DCMD_PROC_TIDSTATUS.sutime.
  char path[64];
  const int len = snprintf(path, sizeof(path), "/proc/%d/as", getpid());
  CHECK(len > 0 && static_cast<size_t>(len) < sizeof(path));

  const int fd = open(path, O_RDONLY);
  CHECK(fd != -1);

  procfs_status status = {};
  status.tid = pthread_self();
  const int rc = devctl(fd, DCMD_PROC_TIDSTATUS, &status, sizeof(status),
                        nullptr);
  close(fd);
  CHECK(rc == EOK);

  return ThreadTicks() + Nanoseconds(static_cast<int64_t>(status.sutime));
}

}  // namespace subtle

// static
TimeTicks::Clock TimeTicks::GetClock() {
  // QNX uses a POSIX monotonic clock; report as the closest equivalent.
  return Clock::LINUX_CLOCK_MONOTONIC;
}

// static
bool TimeTicks::IsHighResolution() {
  return true;
}

// static
bool TimeTicks::IsConsistentAcrossProcesses() {
  return true;
}

}  // namespace base
