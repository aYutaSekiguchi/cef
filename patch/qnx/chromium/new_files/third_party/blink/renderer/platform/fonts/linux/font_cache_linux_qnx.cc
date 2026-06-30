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

// QNX headless builds do not have Skia's font matcher wired up to fontconfig
// (`skia::DefaultFontMgr()` returns the empty `SkFontMgr_New_Custom_Empty`),
// so `matchFamilyStyle()` returns nullptr for every CSS family name and
// `CreateTypeface` would otherwise produce an empty typeface. Without a
// real primary typeface, `SimpleFontData::PlatformInit` reports zero
// ascent/descent, the inline line-box height collapses to 0, and
// `elementFromPoint` cannot hit-test anchors (e.g. the DownloadTest.*
// cases that click at (20, 20) on a `<a>CLICK ME</a>` link).
//
// Fix: in `CreateTypeface`, always return a real system font file
// (DejaVu is shipped on the QEMU image) as the primary typeface so the
// rest of the layout pipeline gets real ascent, descent, and glyph
// widths. See
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

sk_sp<SkTypeface> LoadQnxFallbackTypeface() {
  static const char* const fallback_path = ResolveQnxFallbackFontPath();
  if (!fallback_path) {
    return nullptr;
  }
  return SkTypeface_Factory::FromFilenameAndTtcIndex(fallback_path, 0);
}

}  // namespace

const AtomicString& FontCache::SystemFontFamily() {
  // Return a non-empty name so Blink does not take a code path that
  // bypasses `CreateTypeface` entirely. The actual typeface comes from
  // DejaVu below, regardless of this name.
  static AtomicString family("DejaVu Sans");
  return family;
}

sk_sp<SkTypeface> FontCache::CreateTypeface(
    const FontDescription& font_description,
    const FontFaceCreationParams& creation_params,
    std::string& name) {
  // Skip the default Skia family matching on QNX: it returns nullptr
  // because the default font manager has no system fonts installed.
  // Go straight to the DejaVu fallback so the rest of the layout
  // pipeline gets a real typeface with real metrics.
  sk_sp<SkTypeface> fallback = LoadQnxFallbackTypeface();
  if (!fallback) {
    return nullptr;
  }
  name = "DejaVu Sans";
  return fallback;
}

const SimpleFontData* FontCache::PlatformFallbackFontForCharacter(
    const FontDescription& font_description,
    UChar32 c,
    const SimpleFontData* font_data_to_substitute,
    FontFallbackPriority priority) {
  sk_sp<SkTypeface> fallback = LoadQnxFallbackTypeface();
  if (!fallback) {
    return nullptr;
  }

  float text_size = font_description.ComputedSize();
  if (text_size <= 0.f) {
    text_size = 14.f;
  }

  FontPlatformData* platform_data = MakeGarbageCollected<FontPlatformData>(
      std::move(fallback), "DejaVu Sans", text_size, /*synthetic_bold=*/false,
      /*synthetic_italic=*/false, font_description.TextRendering(),
      ResolvedFontFeatures(), font_description.Orientation());

  return FontDataFromFontPlatformData(platform_data,
                                      font_description.SubpixelAscentDescent());
}

}  // namespace blink