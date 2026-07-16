// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_GPU_TRACE_H_
#define UI_OZONE_PLATFORM_QNX_QNX_GPU_TRACE_H_

#include "base/command_line.h"
#include "base/logging.h"

namespace ui {
namespace ozone {
namespace qnx_trace {

inline constexpr char kSwitch[] = "ozone-qnx-gpu-trace";

inline bool IsEnabled() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch(kSwitch);
}

}  // namespace qnx_trace
}  // namespace ozone
}  // namespace ui

// QNX Ozone diagnostics are intentionally silent unless the explicit trace
// switch is present. LOG_IF keeps message construction out of the hot path
// when tracing is disabled, while still allowing diagnostics in Release builds.
#define QNX_GPU_TRACE_LOG(severity) \
  LOG_IF(severity, ::ui::ozone::qnx_trace::IsEnabled())

#endif  // UI_OZONE_PLATFORM_QNX_QNX_GPU_TRACE_H_
