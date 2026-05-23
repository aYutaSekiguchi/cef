// Copyright 2025 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/threading/platform_thread.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <string.h>
#include <sys/neutrino.h>
#include <sys/resource.h>
#include <unistd.h>

#include <optional>
#include <string>

#include "base/logging.h"
#include "base/task/current_thread.h"
#include "base/threading/platform_thread_internal_posix.h"

namespace base {

namespace {

// QNX Neutrino priority range: 1 (lowest) to 63 (highest).
// Default thread priority is 10.
constexpr int kQnxDefaultPriority = 10;

int ThreadTypeToQnxPriority(ThreadType thread_type) {
  switch (thread_type) {
    case ThreadType::kBackground:
      return 5;
    case ThreadType::kUtility:
      return 8;
    case ThreadType::kDefault:
      return kQnxDefaultPriority;
    case ThreadType::kPresentation:
    case ThreadType::kAudioProcessing:
      return 15;
    case ThreadType::kRealtimeAudio:
      return 25;
  }
}

ThreadType QnxPriorityToThreadType(int priority) {
  if (priority >= 22) {
    return ThreadType::kRealtimeAudio;
  }
  if (priority >= 13) {
    return ThreadType::kPresentation;
  }
  if (priority >= 9) {
    return ThreadType::kDefault;
  }
  if (priority >= 6) {
    return ThreadType::kUtility;
  }
  return ThreadType::kBackground;
}

int GetSchedPolicyForThreadType(ThreadType thread_type) {
  switch (thread_type) {
    case ThreadType::kRealtimeAudio:
      return SCHED_FIFO;
    default:
      return SCHED_RR;
  }
}

// Sets scheduling for thread |tid| in the current process using QNX SchedSet.
// Returns true on success.
bool QnxSetScheduling(int tid, int policy, int priority) {
  struct sched_param param = {};
  param.sched_priority = priority;
  if (SchedSet(0, tid, policy, &param) == -1) {
    DLOG(WARNING) << "SchedSet(tid=" << tid << " policy=" << policy
                  << " priority=" << priority
                  << ") failed: " << strerror(errno)
                  << " (errno=" << errno << ")";
    return false;
  }
  return true;
}

// Gets scheduling priority for thread |tid| in the current process using QNX
// SchedGet. Returns true on success.
bool QnxGetScheduling(int tid, int* policy, int* priority) {
  struct sched_param param = {};
  if (SchedGet(0, tid, &param) == -1) {
    DLOG(WARNING) << "SchedGet(tid=" << tid << ") failed: " << strerror(errno)
                  << " (errno=" << errno << ")";
    return false;
  }
  if (policy) {
    *policy = SCHED_RR;
  }
  if (priority) {
    *priority = param.sched_priority;
  }
  return true;
}

}  // namespace

namespace internal {

// Left for compatibility with the POSIX fallback path in
// GetCurrentEffectiveThreadTypeForTest(). Not actively used since
// GetCurrentEffectiveThreadTypeForPlatformForTest() returns a value on QNX.
const ThreadTypeToNiceValuePairForTest
    kThreadTypeToNiceValueMapForTest[] = {
        {ThreadType::kRealtimeAudio, -8},
        {ThreadType::kDefault, 0},
        {ThreadType::kUtility, 1},
        {ThreadType::kBackground, 10},
};

int ThreadTypeToNiceValue(ThreadType thread_type) {
  switch (thread_type) {
    case ThreadType::kBackground:
      return 10;
    case ThreadType::kUtility:
      return 1;
    case ThreadType::kDefault:
      return 0;
    case ThreadType::kPresentation:
    case ThreadType::kAudioProcessing:
      return -4;
    case ThreadType::kRealtimeAudio:
      return -8;
  }
}

bool CanSetThreadTypeToRealtimeAudio() {
  // Allow realtime audio priority for root users.
  return geteuid() == 0;
}

void SetCurrentThreadTypeImpl(ThreadType thread_type,
                              MessagePumpType /*pump_type_hint*/) {
  // pthread_t on QNX is _Int32t (the kernel TID/LWP ID), so we can use
  // SchedSet directly.
  const int tid = static_cast<int>(pthread_self());
  const int policy = GetSchedPolicyForThreadType(thread_type);
  const int priority = ThreadTypeToQnxPriority(thread_type);
  QnxSetScheduling(tid, policy, priority);
}

PlatformPriorityOverride SetThreadTypeOverride(
    PlatformThreadHandle thread_handle,
    ThreadType thread_type) {
  const pthread_t tid = thread_handle.platform_handle();
  if (tid == 0) {
    return false;
  }

  // Check current priority; if already at or above the target, no change
  // needed.
  int current_priority = 0;
  if (QnxGetScheduling(tid, nullptr, &current_priority)) {
    if (current_priority >= ThreadTypeToQnxPriority(thread_type)) {
      return false;
    }
  }

  const int policy = GetSchedPolicyForThreadType(thread_type);
  const int priority = ThreadTypeToQnxPriority(thread_type);
  return QnxSetScheduling(tid, policy, priority);
}

void RemoveThreadTypeOverride(
    PlatformThreadHandle thread_handle,
    const PlatformPriorityOverride& priority_override_handle,
    ThreadType initial_thread_type) {
  if (!priority_override_handle) {
    return;
  }
  const pthread_t tid = thread_handle.platform_handle();
  if (tid == 0) {
    return;
  }
  const int policy = GetSchedPolicyForThreadType(initial_thread_type);
  const int priority = ThreadTypeToQnxPriority(initial_thread_type);
  QnxSetScheduling(tid, policy, priority);
}

std::optional<ThreadType>
GetCurrentEffectiveThreadTypeForPlatformForTest() {
  const int tid = static_cast<int>(pthread_self());
  int priority = 0;
  if (!QnxGetScheduling(tid, nullptr, &priority)) {
    return std::nullopt;
  }
  return QnxPriorityToThreadType(priority);
}

}  // namespace internal

// static
void PlatformThreadBase::SetName(const std::string& name) {
  SetNameCommon(name);

  // Use QNX-native thread name API.  The struct is variable-length:
  // sizeof(struct _thread_name) + name length (including NUL).
  const size_t max_len = _NTO_THREAD_NAME_MAX - 1;
  const size_t name_len = std::min(name.size(), max_len);

  // Allocate on the stack (name is short).
  char buf[sizeof(struct _thread_name) + _NTO_THREAD_NAME_MAX] = {};
  struct _thread_name* tn = reinterpret_cast<struct _thread_name*>(buf);
  tn->new_name_len = static_cast<int>(name_len);
  tn->name_buf_len = static_cast<int>(name_len + 1);
  memcpy(tn->name_buf, name.data(), name_len);
  tn->name_buf[name_len] = '\0';

  if (ThreadCtl(_NTO_TCTL_NAME, tn) == -1) {
    DPLOG(ERROR) << "ThreadCtl(_NTO_TCTL_NAME) failed";
  }
}

// static
bool PlatformThreadBase::CanChangeThreadType(ThreadType from, ThreadType to) {
  if (from >= to) {
    // Decreasing thread priority is always allowed.
    return true;
  }
  if (to == ThreadType::kRealtimeAudio) {
    return internal::CanSetThreadTypeToRealtimeAudio();
  }
  // Increasing thread priority via SchedSet requires PROCMGR_AID_PRIORITY
  // capability. Since tests run as root in QEMU, all transitions are allowed.
  return geteuid() == 0;
}

// These functions are declared in platform_thread_posix.cc and called from it.
// On Linux they are provided by platform_thread_linux_base.cc.

void InitThreading() {}

void TerminateOnThread() {}

size_t GetDefaultThreadStackSize(const pthread_attr_t& attributes) {
  size_t stack_size = 0;
  pthread_attr_getstacksize(&attributes, &stack_size);
  return stack_size;
}

}  // namespace base
