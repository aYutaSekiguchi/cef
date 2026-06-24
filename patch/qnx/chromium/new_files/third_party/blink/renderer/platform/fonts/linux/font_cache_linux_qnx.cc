// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/font_cache.h"
#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"

namespace blink {

const AtomicString& FontCache::SystemFontFamily() {
  static AtomicString empty;
  return empty;
}

const SimpleFontData* FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& desc,
    UChar32 c,
    const SimpleFontData* font_data_to_substitute,
    FontFallbackPriority priority) {
  return nullptr;
}

}  // namespace blink
