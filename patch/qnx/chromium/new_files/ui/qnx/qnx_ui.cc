// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/qnx/qnx_ui.h"

#include "base/notimplemented.h"

namespace ui {

namespace {

// Stub implementation used until ozone is ported to QNX. Always reports
// animations as enabled to preserve the existing Chromium default behavior
// on platforms without a real toolkit integration. Replace with a real
// implementation that consults the QNX desktop settings when the ozone port
// lands.
class StubQnxUi : public QnxUi {
 public:
  StubQnxUi() = default;
  ~StubQnxUi() override = default;

  bool AnimationsEnabled() const override {
    // TODO(crbug.com/qnx-port): replace with a real implementation that
    // consults the QNX desktop/UI setting once ozone is ported to QNX.
    // Default to true to preserve the Chromium default behavior on platforms
    // without a real toolkit integration.
    NOTIMPLEMENTED();
    return true;
  }
};

}  // namespace

// static
QnxUi* QnxUi::instance() {
  static StubQnxUi instance;
  return &instance;
}

}  // namespace ui