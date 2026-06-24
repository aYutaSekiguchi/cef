// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_theme.h"

namespace blink {

LayoutTheme& LayoutTheme::NativeTheme() {
  static LayoutTheme theme;
  return theme;
}

}  // namespace blink
