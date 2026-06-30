// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "third_party/blink/renderer/platform/fonts/font_cache.h"

#include <unistd.h>

#include <string>

#include "third_party/blink/renderer/platform/fonts/font_description.h"
#include "third_party/blink/renderer/platform/fonts/font_platform_data.h"
#include "third_party/blink/renderer/platform/fonts/simple_font_data.h"
#include "third_party/blink/renderer/platform/fonts/skia/sktypeface_factory.h"

namespace blink {

namespace {

// QNX headless builds do not have Skia's font matcher wired up to fontconfig,
// so `PlatformFallbackFontForCharacter` would otherwise return nullptr and
// every text run would collapse to 0x0. Fall back to a real system font file
// (DejaVu is shipped on the QEMU image) so text metrics are measurable and
// clickable inline elements get a hit-test area. See
// docs/qnx/history/build-errors/test/runtime-assumption/qnx-ceftests-text-zero-width.md.
const char* const kQnxFallbackFontPaths[] = {
    "/usr/share/fonts/DejaVuLGCSans.ttf",
    "/usr/share/fonts/DejaVuSans.ttf",
    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/Roboto.ttf",
};

bool FileReadable(const char* path) {
  return path && access(path, R_OK) == 0;
}

const char* ResolveQnxFallbackFontPath() {
  for (const char* path : kQnxFallbackFontPaths) {
    if (FileReadable(path)) {
      return path;
    }
  }
  return nullptr;
}

}  // namespace

const AtomicString& FontCache::SystemFontFamily() {
  // Return a name so Blink accepts the system family lookup. The fallback
  // path is what actually provides the typeface.
  static AtomicString family("DejaVu Sans");
  return family;
}

const SimpleFontData* FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& font_description,
    UChar32 c,
    const SimpleFontData* font_data_to_substitute,
    FontFallbackPriority priority) {
  static const char* const fallback_path = ResolveQnxFallbackFontPath();
  if (!fallback_path) {
    return nullptr;
  }

  sk_sp<SkTypeface> typeface =
      SkTypeface_Factory::FromFilenameAndTtcIndex(fallback_path, 0);
  if (!typeface) {
    return nullptr;
  }

  // FontDescription::ComputedSize() is 0 when Blink has not yet committed a
  // size for this lookup. Force a sane default so SimpleFontData uses our
  // typeface via FontPlatformData::CreateSkFont() instead of falling back to
  // skia::DefaultFont(), which on QNX returns the empty FontMgr typeface.
  float text_size = font_description.ComputedSize();
  if (text_size <= 0.f) {
    text_size = 14.f;
  }

  FontPlatformData* platform_data = MakeGarbageCollected<FontPlatformData>(
      std::move(typeface), "DejaVu Sans", text_size, /*synthetic_bold=*/false,
      /*synthetic_italic=*/false, font_description.TextRendering(),
      ResolvedFontFeatures(), font_description.Orientation());

  return FontDataFromFontPlatformData(platform_data,
                                      font_description.SubpixelAscentDescent());
}

}  // namespace blink