// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/core/layout/layout_theme_default.h"

#include "base/memory/ref_counted.h"

namespace blink {

class LayoutThemeQnx final : public LayoutThemeDefault {
 public:
  static scoped_refptr<LayoutTheme> Create() {
    return base::AdoptRef(new LayoutThemeQnx());
  }
};

LayoutTheme& LayoutTheme::NativeTheme() {
  static scoped_refptr<LayoutTheme> theme = LayoutThemeQnx::Create();
  return *theme;
}

}  // namespace blink
