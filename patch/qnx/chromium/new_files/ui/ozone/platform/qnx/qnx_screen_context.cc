// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/qnx/qnx_screen_context.h"
#include "ui/ozone/platform/qnx/qnx_gpu_trace.h"

#include <errno.h>
#include <string.h>

#include "base/logging.h"

namespace ui {
namespace {

const char* SafeStrError(int error_number) {
  const char* message = strerror(error_number);
  return message ? message : "unknown error";
}

}  // namespace

QnxScreenContext::QnxScreenContext() {
  // QNX Screen requires a context before any window can be created.
  // SCREEN_APPLICATION_CONTEXT flags the context as application-owned,
  // suitable for a Chromium/CEF browser process.
  int rc = screen_create_context(&context_, SCREEN_APPLICATION_CONTEXT);
  if (rc != 0) {
    const int saved_errno = errno;
    LOG(ERROR) << "QnxScreenContext: screen_create_context failed: errno="
               << saved_errno << " (" << SafeStrError(saved_errno) << ")";
    context_ = nullptr;
    return;
  }
  QNX_GPU_TRACE_LOG(INFO) << "QnxScreenContext: initialized (context=" << context_ << ")";
}

QnxScreenContext::~QnxScreenContext() {
  if (context_) {
    screen_destroy_context(context_);
    QNX_GPU_TRACE_LOG(INFO) << "QnxScreenContext: destroyed";
  }
}

}  // namespace ui
