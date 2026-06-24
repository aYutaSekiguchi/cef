// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_QNX_QNX_UI_H_
#define UI_QNX_QNX_UI_H_

#include "base/component_export.h"

namespace ui {

// Abstract base class for the QNX UI integration.
//
// The QNX build does not depend on //ui/linux:linux_ui because that target's
// BUILD.gn asserts is_linux. //ui/qnx:qnx_ui is therefore the QNX analogue
// and exposes only the surface that QNX-aware callers (notably
// //ui/gfx/animation) need today.
//
// Future QNX UI work (ozone integration, native theme, window theming, etc.)
// should add virtual methods here and override them in the concrete
// implementation returned by QnxUi::instance().
class COMPONENT_EXPORT(QNX_UI) QnxUi {
 public:
  // Returns the process-wide QnxUi singleton. The current QNX build always
  // installs a stub at startup, so this never returns nullptr; callers do
  // not need a null check before dereferencing.
  static QnxUi* instance();

  virtual ~QnxUi() = default;

  // Returns true if platform-wide animations should be enabled. Mirrors the
  // contract of ui::LinuxUi::AnimationsEnabled() so //ui/gfx/animation can
  // share the same algorithm across platforms.
  //
  // TODO(crbug.com/qnx-port): Replace the stub default once ozone is ported
  // to QNX and the real desktop/UI setting is available.
  virtual bool AnimationsEnabled() const = 0;
};

}  // namespace ui

#endif  // UI_QNX_QNX_UI_H_