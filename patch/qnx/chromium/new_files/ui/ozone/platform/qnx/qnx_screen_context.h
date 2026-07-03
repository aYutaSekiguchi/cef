// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_OZONE_PLATFORM_QNX_QNX_SCREEN_CONTEXT_H_
#define UI_OZONE_PLATFORM_QNX_QNX_SCREEN_CONTEXT_H_

#include <screen/screen.h>

#include "base/component_export.h"

namespace ui {

// Owns the QNX Screen context and provides access to it.
// Initialized once in OzonePlatformQnx::InitializeUI() and destroyed
// when the OzonePlatformQnx is destroyed.
class COMPONENT_EXPORT(OZONE_BASE) QnxScreenContext {
 public:
  QnxScreenContext();
  ~QnxScreenContext();

  QnxScreenContext(const QnxScreenContext&) = delete;
  QnxScreenContext& operator=(const QnxScreenContext&) = delete;

  // Returns the owned screen_context_t. Valid after construction.
  screen_context_t context() const { return context_; }

  // Returns true if the Screen context was created successfully.
  bool is_valid() const { return context_ != nullptr; }

 private:
  screen_context_t context_ = nullptr;
};

}  // namespace ui

#endif  // UI_OZONE_PLATFORM_QNX_QNX_SCREEN_CONTEXT_H_
