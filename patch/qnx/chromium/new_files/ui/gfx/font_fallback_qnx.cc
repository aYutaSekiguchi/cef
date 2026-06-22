// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/gfx/font_fallback.h"

#include "ui/gfx/font.h"

namespace gfx {

std::vector<Font> GetFallbackFonts(const Font& font) {
  return std::vector<Font>();
}

bool GetFallbackFont(const Font& font,
                     const std::string& locale,
                     std::u16string_view text,
                     Font* result) {
  return false;
}

}  // namespace gfx
